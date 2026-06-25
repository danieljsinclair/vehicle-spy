// ESP32 WiFi OTA update — Ed25519ph-signed firmware over HTTP.
//
// Uses the standard HTTPUpdateServer for the upload mechanism.
// After the upload completes, we verify the Ed25519ph signature
// BEFORE the HTTP response is sent. If verification fails,
// the update is aborted and rollback is triggered.

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include "OtaPublicKey.h"

// ── Named Constants ────────────────────────────────────────────────────────────
namespace OtaConstants {
    static constexpr uint16_t HTTP_PORT = 80;
    static constexpr uint32_t UPLOAD_TIMEOUT_MS = 300000;  // 5 minutes
    static constexpr uint32_t VERIFY_YIELD_INTERVAL = 0x3FFF;  // WDT feed every 16KB
    static constexpr uint32_t VERIFY_CHUNK_SIZE = 512;
    static constexpr uint32_t REBOOT_FLUSH_COUNT = 10;
    static constexpr uint32_t REBOOT_FLUSH_DELAY_MS = 100;
    static constexpr uint32_t REBOOT_DELAY_MS = 100;

    // HTTP response codes
    static constexpr int HTTP_OK = 200;
    static constexpr int HTTP_UNAUTHORIZED = 401;
    static constexpr int HTTP_FORBIDDEN = 403;
    static constexpr int HTTP_INSUFFICIENT_STORAGE = 507;
    static constexpr int HTTP_BAD_REQUEST = 400;
}

extern "C" {
#include <sodium.h>
}

static const char* OTA_SIG_HDR = "X-Firmware-Signature";

static WebServer otaHttp(OtaConstants::HTTP_PORT);
static HTTPUpdateServer otaUpdater;
static bool sodiumReady = false;
static uint32_t otaUploadStartTime = 0;

// ── Signature verification (called after upload, before HTTP response) ──
static uint8_t otaSig[64];
static bool otaHasSig = false;
static String otaErr;

// OTA error codes for better host diagnostics
enum class OtaError {
    NONE,
    SODIUM_NOT_READY,
    BAD_SIGNATURE_HEADER,
    NO_SIGNATURE,
    UPDATE_BEGIN_FAILED,
    UPDATE_WRITE_FAILED,
    UPDATE_END_FAILED,
    UPDATE_ERROR,
    NO_OTA_PARTITION,
    SIGNATURE_VERIFY_FAILED,
    SET_BOOT_PARTITION_FAILED,
    UPLOAD_ABORTED,
    UPLOAD_TIMEOUT
};

// Forward declarations
static String otaErrorMessage(OtaError err);
static int otaHttpCode(OtaError err);

// Testable pure function: Hex byte conversion (testable unit)
static bool hexToByte(char c, uint8_t& v) {
    if (c >= '0' && c <= '9') { v = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
    return false;
}

// Testable pure function: Parse hex signature (testable unit)
static bool parseHexSig(const String& hex, uint8_t* out) {
    if (hex.length() != 128) return false;
    for (size_t i = 0; i < 64; i++) {
        uint8_t hi = 0, lo = 0;
        if (!hexToByte(hex[i*2], hi) || !hexToByte(hex[i*2+1], lo)) return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}

// Verify partition signature with WDT feeding to prevent boot-loops
static bool verifyPartition(const esp_partition_t* part, uint32_t size, const uint8_t* sig) {
    if (!part || size > part->size) {
        Serial.printf("%sOTA: invalid partition or size%s\r\n", RED, NC);
        return false;
    }

    crypto_sign_ed25519ph_state state;
    if (crypto_sign_ed25519ph_init(&state) != 0) {
        Serial.printf("%sOTA: signature init failed%s\r\n", RED, NC);
        return false;
    }

    uint8_t chunk[OtaConstants::VERIFY_CHUNK_SIZE];
    uint32_t off = 0;
    while (off < size) {
        size_t n = (size - off < OtaConstants::VERIFY_CHUNK_SIZE) ?
                   (size - off) : OtaConstants::VERIFY_CHUNK_SIZE;

        if (esp_partition_read(part, off, chunk, n) != ESP_OK) {
            Serial.printf("%sOTA: partition read failed at offset %u%s\r\n", RED, off, NC);
            return false;
        }

        if (crypto_sign_ed25519ph_update(&state, chunk, n) != 0) {
            Serial.printf("%sOTA: signature update failed%s\r\n", RED, NC);
            return false;
        }

        off += n;

        // Feed WDT periodically to prevent boot-loop after bad flash
        if ((off & OtaConstants::VERIFY_YIELD_INTERVAL) == 0) {
            delay(1);
        }
    }

    const bool valid = crypto_sign_ed25519ph_final_verify(&state, sig, OTA_PUBLIC_KEY) == 0;
    if (!valid) {
        Serial.printf("%sOTA: signature verification failed%s\r\n", RED, NC);
    }
    return valid;
}

static String otaErrorMessage(OtaError err) {
    switch (err) {
        case OtaError::SODIUM_NOT_READY: return "sodium library not ready";
        case OtaError::BAD_SIGNATURE_HEADER: return "invalid signature header format";
        case OtaError::NO_SIGNATURE: return "missing signature header";
        case OtaError::UPDATE_BEGIN_FAILED: return "failed to begin update";
        case OtaError::UPDATE_WRITE_FAILED: return "firmware write failed";
        case OtaError::UPDATE_END_FAILED: return "failed to end update";
        case OtaError::UPDATE_ERROR: return "update error";
        case OtaError::NO_OTA_PARTITION: return "no OTA partition available";
        case OtaError::SIGNATURE_VERIFY_FAILED: return "signature verification failed";
        case OtaError::SET_BOOT_PARTITION_FAILED: return "failed to set boot partition";
        case OtaError::UPLOAD_ABORTED: return "upload aborted";
        case OtaError::UPLOAD_TIMEOUT: return "upload timeout";
        default: return "unknown error";
    }
}

static int otaHttpCode(OtaError err) {
    switch (err) {
        case OtaError::NONE: return OtaConstants::HTTP_OK;
        case OtaError::SIGNATURE_VERIFY_FAILED:
        case OtaError::BAD_SIGNATURE_HEADER:
            return OtaConstants::HTTP_FORBIDDEN;
        case OtaError::UPLOAD_TIMEOUT:
            return OtaConstants::HTTP_BAD_REQUEST;
        case OtaError::NO_SIGNATURE:
            return OtaConstants::HTTP_UNAUTHORIZED;
        default:
            return OtaConstants::HTTP_BAD_REQUEST;
    }
}

void otaSetup() {
    if (sodium_init() >= 0) {
        sodiumReady = true;
        Serial.println("OTA: sodium crypto library initialized");
    } else {
        Serial.printf("%sOTA: sodium crypto library failed to initialize%s\r\n", RED, NC);
    }

    static const char* hdrs[] = { OTA_SIG_HDR };
    otaHttp.collectHeaders(hdrs, 1);

    // Use HTTPUpdateServer with no auth (signature is the security boundary)
    otaUpdater.setup(&otaHttp, "/update", "", "");

    // Override GET handler for the upload form
    otaHttp.on("/update", HTTP_GET, []() {
        otaHttp.send(OtaConstants::HTTP_OK, "text/html",
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
                // Parse error from otaErr string
                OtaError err = OtaError::UPDATE_ERROR;
                String errLower = otaErr;
                errLower.toLowerCase();

                if (errLower.indexOf("signature") >= 0) {
                    err = OtaError::SIGNATURE_VERIFY_FAILED;
                } else if (errLower.indexOf("sodium") >= 0) {
                    err = OtaError::SODIUM_NOT_READY;
                }

                int code = otaHttpCode(err);
                otaHttp.send(code, "text/plain", "OTA error: " + otaErr);
                Serial.printf("%sOTA upload failed: %s (HTTP %d)%s\r\n", RED, otaErr.c_str(), code, NC);
            } else if (Update.hasError()) {
                otaHttp.send(OtaConstants::HTTP_BAD_REQUEST, "text/plain", "OTA error: update failed");
                Serial.printf("%sOTA upload failed: update error%s\r\n", RED, NC);
            } else {
                otaHttp.client().setNoDelay(true);
                otaHttp.send(OtaConstants::HTTP_OK, "text/plain", "Update Success! Rebooting...");
                Serial.printf("%sOTA: accepted firmware, rebooting%s\r\n", GREEN, NC);

                for (int i = 0; i < OtaConstants::REBOOT_FLUSH_COUNT; i++) {
                    otaHttp.client().flush();
                    delay(OtaConstants::REBOOT_FLUSH_DELAY_MS);
                }
                otaHttp.client().stop();
                delay(OtaConstants::REBOOT_DELAY_MS);
                ESP.restart();
            }
        },
        /* upload-handler: called during multipart parsing */
        []() {
            HTTPUpload& up = otaHttp.upload();

            if (up.status == UPLOAD_FILE_START) {
                otaErr.clear();
                otaHasSig = false;
                otaUploadStartTime = millis();

                if (!sodiumReady) {
                    otaErr = otaErrorMessage(OtaError::SODIUM_NOT_READY);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }

                String sig = otaHttp.header(OTA_SIG_HDR);
                if (!parseHexSig(sig, otaSig)) {
                    otaErr = otaErrorMessage(OtaError::BAD_SIGNATURE_HEADER);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }
                otaHasSig = true;

                uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin(maxSpace, U_FLASH)) {
                    otaErr = otaErrorMessage(OtaError::UPDATE_BEGIN_FAILED);
                    Serial.printf("%sOTA: %s (insufficient space: %u bytes)%s\r\n",
                                    RED, otaErr.c_str(), maxSpace, NC);
                    return;
                }

                Serial.printf("OTA: upload started (max size: %u bytes)\r\n", maxSpace);

            } else if (up.status == UPLOAD_FILE_WRITE && otaErr.isEmpty()) {
                // Check for upload timeout
                if ((millis() - otaUploadStartTime) > OtaConstants::UPLOAD_TIMEOUT_MS) {
                    otaErr = otaErrorMessage(OtaError::UPLOAD_TIMEOUT);
                    Serial.printf("%sOTA: upload timeout after %lu ms%s\r\n",
                                    RED, millis() - otaUploadStartTime, NC);
                    Update.abort();
                    return;
                }

                if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                    otaErr = otaErrorMessage(OtaError::UPDATE_WRITE_FAILED);
                    Serial.printf("%sOTA: write failed at offset %u%s\r\n", RED, up.totalSize, NC);
                    return;
                }
                delay(0);  // Feed WDT

            } else if (up.status == UPLOAD_FILE_END && otaErr.isEmpty()) {
                Serial.printf("OTA: upload completed (%u bytes)\r\n", up.totalSize);

                if (!otaHasSig) {
                    Update.abort();
                    otaErr = otaErrorMessage(OtaError::NO_SIGNATURE);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }

                if (!Update.end(true)) {
                    otaErr = otaErrorMessage(OtaError::UPDATE_END_FAILED);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }

                if (Update.hasError()) {
                    otaErr = otaErrorMessage(OtaError::UPDATE_ERROR);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }

                // Verify signature before committing
                auto* running = esp_ota_get_running_partition();
                auto* target = esp_ota_get_next_update_partition(running);

                if (!target) {
                    otaErr = otaErrorMessage(OtaError::NO_OTA_PARTITION);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }

                if (!verifyPartition(target, up.totalSize, otaSig)) {
                    otaErr = otaErrorMessage(OtaError::SIGNATURE_VERIFY_FAILED);
                    // Abort update and trigger rollback
                    Update.abort();
                    Serial.printf("%sOTA: signature verification failed, update aborted%s\r\n", RED, NC);
                    return;
                }

                if (esp_ota_set_boot_partition(target) != ESP_OK) {
                    otaErr = otaErrorMessage(OtaError::SET_BOOT_PARTITION_FAILED);
                    Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
                    return;
                }

                Serial.printf("%sOTA: firmware verified and accepted, will reboot%s\r\n", GREEN, NC);

            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
                otaErr = otaErrorMessage(OtaError::UPLOAD_ABORTED);
                Serial.printf("%sOTA: %s%s\r\n", RED, otaErr.c_str(), NC);
            }
        }
    );

    otaHttp.begin();
    Serial.printf("OTA HTTP server on port %u\r\n", OtaConstants::HTTP_PORT);
}

void otaLoop() { otaHttp.handleClient(); }

void otaMarkValidOnBoot() {
    auto* running = esp_ota_get_running_partition();
    if (!running) return;

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.printf("%sOTA: marked current firmware as valid (rollback cancelled)%s\r\n", GREEN, NC);
        }
    } else {
        Serial.printf("%sOTA: unable to get partition state%s\r\n", YELLOW, NC);
    }
}
