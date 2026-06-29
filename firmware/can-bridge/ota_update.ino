// ESP32 WiFi OTA update — Ed25519ph-signed firmware over HTTP.
//
// Uses the standard HTTPUpdateServer for the upload mechanism.
// After the upload completes, we verify the Ed25519ph signature
// BEFORE the HTTP response is sent. If verification fails,
// the update is aborted.

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "OtaPublicKey.h"
#include <string_view>
#include <array>

extern "C" {
#include <sodium.h>
}

static constexpr uint16_t OTA_HTTP_PORT = 80;
static const char* OTA_SIG_HDR = "X-Firmware-Signature";

static WebServer otaHttp(OTA_HTTP_PORT);
static HTTPUpdateServer otaUpdater;
static bool sodiumReady = false;

// ── Signature verification (called after upload, before HTTP response) ──
static uint8_t otaSig[64];
static bool otaHasSig = false;
static String otaErr;

static bool hexToByte(char c, uint8_t& v) {
    if (c >= '0' && c <= '9') { v = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
    return false;
}

static bool parseHexSig(const String& hex, uint8_t* out) {
    if (hex.length() != 128) return false;
    for (size_t i = 0; i < 64; i++) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!hexToByte(hex[i*2], hi) || !hexToByte(hex[i*2+1], lo)) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

static bool verifyPartition(const esp_partition_t* part, uint32_t size, const uint8_t* sig) {
    if (!part || size > part->size) return false;
    crypto_sign_ed25519ph_state state;
    if (crypto_sign_ed25519ph_init(&state) != 0) return false;
    // Use a small stack buffer to avoid heap fragmentation
    uint8_t chunk[512];
    uint32_t off = 0;
    while (off < size) {
        size_t n = (size - off < 512) ? (size - off) : 512;
        if (esp_partition_read(part, off, chunk, n) != ESP_OK) return false;
        if (crypto_sign_ed25519ph_update(&state, chunk, n) != 0) return false;
        off += n;
        // Yield to prevent watchdog timeout during long reads
        if ((off & 0x3FFF) == 0) delay(1);
    }
    return crypto_sign_ed25519ph_final_verify(&state, sig, OTA_PUBLIC_KEY) == 0;
}

void otaSetup() {
    if (sodium_init() >= 0) sodiumReady = true;

    static const char* hdrs[] = { OTA_SIG_HDR };
    otaHttp.collectHeaders(hdrs, 1);

    // Use HTTPUpdateServer with no auth (signature is the security boundary)
    otaUpdater.setup(&otaHttp, "/update", "", "");

    // Override GET handler for the upload form
    otaHttp.on("/update", HTTP_GET, []() {
        otaHttp.send(200, "text/html",
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "Firmware: <input type='file' name='firmware'><br>"
            "Signature (hex): <input name='X-Firmware-Signature'><br>"
            "<input type='submit' value='Update'></form>");
    });

    // Override POST handler to add signature verification
    otaHttp.on("/update", HTTP_POST,
        /* post-handler: called after all uploads complete */
        []() {
            if (otaErr.length()) {
                otaHttp.send(400, "text/plain", "OTA error: " + otaErr);
            } else if (Update.hasError()) {
                otaHttp.send(400, "text/plain", "OTA error: update failed");
            } else {
                otaHttp.client().setNoDelay(true);
                otaHttp.send(200, "text/plain", "Update Success! Rebooting...");
                for (int i = 0; i < 10; i++) { otaHttp.client().flush(); delay(100); }
                otaHttp.client().stop();
                delay(100);
                ESP.restart();
            }
        },
        /* upload-handler: called during multipart parsing */
        []() {
            HTTPUpload& up = otaHttp.upload();
            if (up.status == UPLOAD_FILE_START) {
                otaErr.clear();
                otaHasSig = false;
                if (!sodiumReady) { otaErr = "sodium not ready"; return; }
                String sig = otaHttp.header(OTA_SIG_HDR);
                if (!parseHexSig(sig, otaSig)) { otaErr = "bad signature header"; return; }
                otaHasSig = true;
                uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin(maxSpace, U_FLASH)) { otaErr = "Update.begin failed"; }
            } else if (up.status == UPLOAD_FILE_WRITE && otaErr.isEmpty()) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize)
                    otaErr = "Update.write failed";
                delay(0);
            } else if (up.status == UPLOAD_FILE_END && otaErr.isEmpty()) {
                if (!otaHasSig) { Update.abort(); otaErr = "no signature"; return; }
                if (!Update.end(true)) { otaErr = "Update.end failed"; return; }
                if (Update.hasError()) { otaErr = "update error"; return; }
                // Verify signature before committing
                auto* running = esp_ota_get_running_partition();
                auto* target = esp_ota_get_next_update_partition(running);
                if (!target) { otaErr = "no OTA partition"; return; }
                if (!verifyPartition(target, up.totalSize, otaSig)) {
                    Serial.printf("%sOTA: REJECTED — signature verification failed%s\n", RED, NC);
                    otaErr = "signature verification failed";
                    return;
                }
                if (esp_ota_set_boot_partition(target) != ESP_OK)
                    { otaErr = "set boot partition failed"; return; }
                Serial.printf("%sOTA: accepted %u bytes, rebooting\n%s", GREEN, up.totalSize, NC);
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.end();
                otaErr = "upload aborted";
            }
        }
    );

    otaHttp.begin();
    Serial.printf("%sOTA HTTP server on port %u\n%s", GREEN, OTA_HTTP_PORT, NC);
}

void otaLoop() { otaHttp.handleClient(); }

void otaMarkValidOnBoot() {
    auto* running = esp_ota_get_running_partition();
    if (!running) return;
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.printf("%sOTA: marked valid%s\n", GREEN, NC);
        }
    }
}
