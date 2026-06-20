// ESP32 WiFi OTA update server — signed-image, verify-before-commit.
//
// Listens on TCP port 3334 (additive to the CAN bridge on 3333). Receives a
// firmware image plus a detached ed25519 signature, verifies the signature
// against the baked-in per-user public key (OtaPublicKey.h) using libsodium
// (shipped in ESP-IDF, linked by default), and ONLY THEN commits the image
// to the OTA partition via the Arduino `Update` library.
//
// SECURITY MODEL
//   - App-level software signature PKI (not hardware secure-boot/eFuse).
//   - Host SIGNS each image with an ed25519 private key (never in the repo).
//   - Device VERIFIES with a baked-in public key before flashing.
//   - Bad signature => reject, never flash.
//   - OTA rollback: new firmware marks itself valid on a healthy boot
//     (otaMarkValidOnBoot), else the device rolls back next reboot.
//
// PROTOCOL (host -> device, binary, little-endian)
//   1. magic        : 4 bytes  = "OTA1"
//   2. image_size   : uint32 LE (firmware bytes; 1..MAX_IMAGE_SIZE)
//   3. sig_size     : uint8    (must be 64)
//   4. signature    : sig_size bytes (detached ed25519 over the image)
//   5. image        : image_size bytes (raw firmware .bin)
//
//   Reply (one line, ASCII):  "OK\n"  | "ERR:<reason>\n"
//
// WHY WRITE-THEN-VERIFY (not verify-then-write)
//   We stream the image into the OTA partition via Update.write(), then read
//   the written bytes back (esp_partition_read over the new OTA partition) and
//   verify the signature over them. Update.end(true) (which commits +
//   MD5-checks) is called ONLY if the signature validates; otherwise
//   Update.abort() discards the write and the running firmware is untouched.
//
// SIGNING SCHEME: Ed25519ph (pre-hashed, RFC 8032)
//   The host signs with crypto_sign_ed25519ph (SHA-512 pre-hash of the image,
//   then sign the 64-byte hash). The device verifies with
//   crypto_sign_ed25519ph_final_verify, streaming image bytes from flash
//   through libsodium's incremental SHA-512 state — only ~200 bytes of stack
//   needed, no malloc. This is standard libsodium, not custom crypto.

#include <WiFi.h>
#include <WiFiClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "OtaPublicKey.h"

// libsodium ed25519 verify (shipped in ESP-IDF, linked by default)
extern "C" {
#include <sodium.h>
}

// ── Config ──────────────────────────────────────────────────────────────
static constexpr uint32_t OTA_PORT = 3334;

// Largest image we'll accept. The OTA partition must be at least this big;
// the default "Default 4MB with spiffs" scheme has NO OTA partition — flash
// with an OTA scheme (e.g. PartitionScheme=min_spiffs). See Makefile notes.
static constexpr uint32_t OTA_MAX_IMAGE = 2 * 1024 * 1024;  // 2 MiB ceiling

static constexpr size_t OTA_CHUNK = 4096;     // Update.write chunk size
static constexpr uint32_t OTA_RECV_TIMEOUT_MS = 10000;

static constexpr char OTA_MAGIC[4] = {'O', 'T', 'A', '1'};
static constexpr uint8_t OTA_SIG_LEN = 64;     // crypto_sign_ed25519_BYTES

// Status codes for the host
static const char OTA_OK[] = "OK\n";

static WiFiServer otaServer(OTA_PORT);
static bool otaSodiumReady = false;

// ── Internal helpers ────────────────────────────────────────────────────

// Read exactly `want` bytes from `c` within OTA_RECV_TIMEOUT_MS. Returns
// false on timeout/disconnect. Blocking but bounded.
static bool otaRecvExact(WiFiClient& c, uint8_t* dst, size_t want) {
    size_t got = 0;
    uint32_t start = millis();
    while (got < want) {
        int avail = c.read(dst + got, want - got);
        if (avail > 0) {
            got += (size_t)avail;
            start = millis();  // reset idle timer on progress
        } else if (avail == 0) {
            if (!c.connected() || (millis() - start) > OTA_RECV_TIMEOUT_MS) {
                return false;
            }
            delay(1);
        } else {
            return false;  // error
        }
    }
    return true;
}

// Verify the ed25519ph (pre-hashed) signature over the OTA partition using
// libsodium's streaming crypto_sign_ed25519ph API. This needs only a small
// stack buffer — no full-image heap allocation required.
//
// The host signs with crypto_sign_ed25519ph (pre-hashed ed25519), and the
// device verifies with crypto_sign_ed25519ph_final_verify, streaming the
// image bytes through in chunks. This is a standard libsodium API (RFC 8032
// Ed25519ph), not custom crypto.
static bool otaVerifyPartition(const esp_partition_t* ota_partition,
                               uint32_t image_size,
                               const uint8_t* sig) {
    if (ota_partition == nullptr) return false;
    if (image_size > ota_partition->size) return false;

    crypto_sign_ed25519ph_state state;
    if (crypto_sign_ed25519ph_init(&state) != 0) {
        return false;
    }

    // Stream the image from flash through the pre-hashed verifier in chunks
    static uint8_t chunk[OTA_CHUNK];
    uint32_t offset = 0;
    while (offset < image_size) {
        size_t to_read = (image_size - offset < OTA_CHUNK)
                             ? (image_size - offset) : OTA_CHUNK;
        esp_err_t err = esp_partition_read(ota_partition, offset, chunk, to_read);
        if (err != ESP_OK) {
            return false;
        }
        if (crypto_sign_ed25519ph_update(&state, chunk, to_read) != 0) {
            return false;
        }
        offset += (uint32_t)to_read;
    }

    // Finalize and verify the pre-hashed signature
    int v = crypto_sign_ed25519ph_final_verify(&state, sig, OTA_PUBLIC_KEY);
    return (v == 0);
}

// Handle one OTA upload session on `c`. Sends one reply line.
static void otaHandleSession(WiFiClient& c) {
    auto reject = [&](const char* why) {
        c.printf("ERR:%s\n", why);
        c.flush();
    };

    // Fail-closed: never accept an image if the verify primitive isn't ready.
    // An OTA we can't verify is worse than no OTA.
    if (!otaSodiumReady) {
        reject("nosodium");
        return;
    }

    // 1. magic
    char magic[4];
    if (!otaRecvExact(c, (uint8_t*)magic, 4) || memcmp(magic, OTA_MAGIC, 4) != 0) {
        reject("magic");
        return;
    }

    // 2. image_size
    uint32_t image_size = 0;
    if (!otaRecvExact(c, (uint8_t*)&image_size, 4)) {
        reject("size");
        return;
    }
    if (image_size == 0 || image_size > OTA_MAX_IMAGE) {
        reject("badsize");
        return;
    }

    // 3. sig_size + signature
    uint8_t sig_size = 0;
    if (!otaRecvExact(c, &sig_size, 1) || sig_size != OTA_SIG_LEN) {
        reject("siglen");
        return;
    }
    uint8_t sig[OTA_SIG_LEN];
    if (!otaRecvExact(c, sig, OTA_SIG_LEN)) {
        reject("sig");
        return;
    }

    // Locate the OTA target partition (the one we're NOT running from).
    // Update.begin() selects it internally; we look it up for readback-verify.
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* ota_target = esp_ota_get_next_update_partition(running);
    if (ota_target == nullptr) {
        reject("nopartition");  // wrong partition scheme — no OTA slot
        return;
    }
    if (image_size > ota_target->size) {
        reject("toobig");
        return;
    }

    // 4. Stream image into the OTA partition via Update.
    if (!Update.begin(image_size, U_FLASH)) {
        reject("begin");
        return;
    }

    static uint8_t chunk[OTA_CHUNK];
    uint32_t written = 0;
    uint32_t idleStart = millis();
    while (written < image_size) {
        size_t want = (image_size - written < OTA_CHUNK) ? (image_size - written) : OTA_CHUNK;
        int got = c.read(chunk, want);
        if (got > 0) {
            size_t wr = Update.write(chunk, (size_t)got);
            if (wr != (size_t)got) {
                Update.abort();
                reject("write");
                return;
            }
            written += (uint32_t)got;
            idleStart = millis();
        } else if (got == 0) {
            if (!c.connected() || (millis() - idleStart) > OTA_RECV_TIMEOUT_MS) {
                Update.abort();
                reject("timeout");
                return;
            }
            delay(1);
        } else {
            Update.abort();
            reject("read");
            return;
        }
    }

    // 5. Read back the written partition and VERIFY the ed25519 signature
    //    BEFORE committing. Update.end() has NOT been called yet, so the new
    //    image is not bootable regardless of what happens next.
    if (!otaVerifyPartition(ota_target, image_size, sig)) {
        Update.abort();
        reject("signature");
        return;
    }

    // 6. Signature valid -> commit + MD5-verify.
    if (!Update.end(true)) {
        reject("commit");
        return;
    }
    if (Update.hasError()) {
        reject("md5");
        return;
    }

    // 7. Mark the new partition for boot. esp_ota_set_boot_partition makes it
    //    pending-rollback: if the new firmware never marks itself valid, the
    //    bootloader rolls back to this (known-good) image on next reboot.
    esp_err_t eb = esp_ota_set_boot_partition(ota_target);
    if (eb != ESP_OK) {
        reject("setboot");
        return;
    }

    c.print(OTA_OK);
    c.flush();
    Serial.printf("OTA: accepted %u-byte image, rebooting in 2s\n", image_size);
    delay(2000);
    ESP.restart();
}

// ── Public API (called from can-bridge.ino) ────────────────────────────

void otaSetup() {
    if (sodium_init() >= 0) {
        otaSodiumReady = true;
    } else {
        Serial.println("OTA: WARN sodium_init failed — signature verify disabled");
    }
    otaServer.begin();
    Serial.printf("OTA server listening on port %u\n", OTA_PORT);
}

void otaLoop() {
    // Non-blocking: service at most one pending connection per loop tick.
    WiFiClient c = otaServer.accept();
    if (c) {
        c.setTimeout(OTA_RECV_TIMEOUT_MS);
        Serial.println("OTA: client connected");
        otaHandleSession(c);
        c.stop();
    }
}

// Mark this firmware's boot as healthy so the bootloader keeps it (cancels a
// pending rollback). Call ONCE from setup() after critical init (WiFi/TWAI up)
// succeeds. If a freshly-OTA'd firmware hangs/crashes before this runs, the
// bootloader reverts to the previous good image on the next reboot.
void otaMarkValidOnBoot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("OTA: new firmware marked valid (rollback cancelled)");
        }
    }
}
