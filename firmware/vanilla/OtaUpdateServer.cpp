#include "OtaUpdateServer.h"
#include "DiscoveryManager.h"  // ITime full definition (millis() for upload-timeout)
#include <cstring>
#include <algorithm>

namespace esp32_firmware {

// The OTA signing public key is crypto-domain and is owned by the ICrypto
// adapter (ArduinoCrypto sources it from OtaPublicKey.h on device; mocks script
// the verify return). verifyPartition therefore passes a sentinel key to
// signEd25519phFinalVerify; the vanilla carries NO key material — correct for a
// host-testable vanilla lib that must not bake a per-user signing key.
static const uint8_t SENTINEL_PUBLIC_KEY[32] = {0};

OtaUpdateServer::OtaUpdateServer(IHttpServer& http, IHttpUpdateServer& updater,
                                 IUpdate& update, IPartition& partition, ICrypto& crypto,
                                 ITime& time)
    : http_(http), updater_(updater), update_(update), partition_(partition),
      crypto_(crypto), time_(time) {}

void OtaUpdateServer::setup() {
    if (crypto_.sodiumInit() >= 0) {
        sodiumReady_ = true;
    }

    static const char* hdrs[1] = { OtaConfig::OTA_SIG_HDR };
    http_.collectHeaders(hdrs, 1);

    updater_.setup(&http_, "/update", "", "");

    http_.on("/update", 1, [this]() { handleGet(); });  // HTTP_GET
    // Upload-handler closure: the WebServer invokes this once per multipart
    // chunk and surfaces the in-flight upload via http_.upload(). We snapshot
    // it (translated to IHttpUpload by the adapter) and forward to the
    // START/WRITE/END/ABORTED state machine. The WebServer owns the upload;
    // the snapshot is valid for the duration of this call.
    http_.on("/update", 2, [this]() { handlePost(); },
             [this]() {
                 std::unique_ptr<IHttpUpload> u = http_.upload();
                 if (u) { handleUpload(*u); }
             });  // HTTP_POST

    http_.begin();
}

void OtaUpdateServer::loop() {
    http_.handleClient();
}

void OtaUpdateServer::markValidOnBoot() {
    const OtaPartitionRef* running = partition_.getRunningPartition();
    if (!running) return;

    int state = 0;
    if (partition_.getStatePartition(running, &state) == 0) {
        if (state == 1) {  // ESP_OTA_IMG_PENDING_VERIFY
            partition_.markAppValidCancelRollback();
        }
    }
}

void OtaUpdateServer::setupHandlers() {
    // Handlers are set up in setup()
}

void OtaUpdateServer::handleGet() {
    http_.send(OtaConfig::HTTP_OK, "text/html",
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "Firmware: <input type='file' name='firmware'><br>"
        "Signature (hex): <input name='X-Firmware-Signature'><br>"
        "<input type='submit' value='Update'></form>");
}

void OtaUpdateServer::handlePost() {
    if (!otaErr_.empty()) {
        OtaError err = OtaError::UPDATE_ERROR;
        std::string errLower = otaErr_;
        std::transform(errLower.begin(), errLower.end(), errLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (errLower.find("signature") != std::string::npos) {
            err = OtaError::SIGNATURE_VERIFY_FAILED;
        } else if (errLower.find("sodium") != std::string::npos) {
            err = OtaError::SODIUM_NOT_READY;
        }

        int code = httpCode(err);
        http_.send(code, "text/plain", ("OTA error: " + otaErr_).c_str());
        if (errorCallback_) {
            errorCallback_(err, otaErr_.c_str());
        }
    } else if (update_.hasError()) {
        http_.send(OtaConfig::HTTP_BAD_REQUEST, "text/plain", "OTA error: update failed");
        if (errorCallback_) {
            errorCallback_(OtaError::UPDATE_ERROR, "update failed");
        }
    } else {
        http_.clientSetNoDelay(true);
        http_.send(OtaConfig::HTTP_OK, "text/plain", "Update Success! Rebooting...");

        for (int i = 0; i < OtaConfig::REBOOT_FLUSH_COUNT; i++) {
            http_.clientFlush();
            // In real implementation: delay(OtaConfig::REBOOT_FLUSH_DELAY_MS);
        }
        http_.clientStop();
        // In real implementation: delay(OtaConfig::REBOOT_DELAY_MS); ESP.restart();

        if (successCallback_) {
            successCallback_();
        }
    }
}

void OtaUpdateServer::reportError(OtaError err) {
    otaErr_ = errorMessage(err);
    if (errorCallback_) {
        errorCallback_(err, otaErr_.c_str());
    }
}

void OtaUpdateServer::handleUpload(IHttpUpload& upload) {
    // START: reset per-upload state, then validate crypto + signature + begin.
    if (upload.status == IHttpUpload::UPLOAD_FILE_START) {
        otaErr_.clear();
        otaHasSig_ = false;
        // Stamp the upload start time via the injected clock so the WRITE path
        // can abort a stalled upload after UPLOAD_TIMEOUT_MS (mirrors the inline
        // ota_update.ino behaviour that previously lived here).
        uploadStartTime_ = time_.millis();

        if (!sodiumReady_) {
            reportError(OtaError::SODIUM_NOT_READY);
            return;
        }

        std::string sig = http_.header(OtaConfig::OTA_SIG_HDR);
        if (!parseHexSig(sig, otaSig_.data())) {
            reportError(OtaError::BAD_SIGNATURE_HEADER);
            return;
        }
        otaHasSig_ = true;

        if (!update_.begin(OtaConfig::FLASH_MAX_SIZE, OtaConfig::CMD_FLASH)) {
            reportError(OtaError::UPDATE_BEGIN_FAILED);
            return;
        }
        return;
    }

    // WRITE: append bytes, but only while the upload is still healthy. A failed
    // START leaves otaErr_ non-empty, so subsequent WRITE chunks are no-ops
    // (the sticky-error contract pinned by HandleUpload_Write_AfterFailedStart).
    if (upload.status == IHttpUpload::UPLOAD_FILE_WRITE && otaErr_.empty()) {
        // Abort a stalled upload: if the wall clock has advanced past the
        // UPLOAD_TIMEOUT_MS window since START, give up. Mirrors the inline
        // ota_update.ino timeout guard (millis() - otaUploadStartTime).
        if (time_.millis() - uploadStartTime_ > OtaConfig::UPLOAD_TIMEOUT_MS) {
            update_.abort();
            reportError(OtaError::UPLOAD_TIMEOUT);
            return;
        }
        if (update_.write(upload.buf, upload.currentSize) != upload.currentSize) {
            reportError(OtaError::UPDATE_WRITE_FAILED);
        }
        return;
    }

    // END: finalize + verify signature + select boot partition. Only reached
    // when otaErr_ is still empty (no prior START/WRITE failure).
    if (upload.status == IHttpUpload::UPLOAD_FILE_END && otaErr_.empty()) {
        // Defensive: START must have parsed a signature before reaching END. By
        // construction every START path either sets otaErr_ (so END is skipped)
        // or sets otaHasSig_=true, making this guard unreachable — kept as a
        // 1-line safety net (no test; see gap-3 analysis).
        if (!otaHasSig_) {
            update_.abort();
            reportError(OtaError::NO_SIGNATURE);
            return;
        }

        if (!update_.end(true)) {
            reportError(OtaError::UPDATE_END_FAILED);
            return;
        }
        if (update_.hasError()) {
            reportError(OtaError::UPDATE_ERROR);
            return;
        }

        const OtaPartitionRef* running = partition_.getRunningPartition();
        const OtaPartitionRef* target = partition_.getNextUpdatePartition(running);
        if (!target) {
            reportError(OtaError::NO_OTA_PARTITION);
            return;
        }

        if (!verifyPartition(target, upload.totalSize, otaSig_.data())) {
            update_.abort();
            reportError(OtaError::SIGNATURE_VERIFY_FAILED);
            return;
        }

        if (partition_.setBootPartition(target) != 0) {
            reportError(OtaError::SET_BOOT_PARTITION_FAILED);
            return;
        }
        return;
    }

    // ABORTED: tear down the in-flight update regardless of prior state.
    if (upload.status == IHttpUpload::UPLOAD_FILE_ABORTED) {
        update_.abort();
        reportError(OtaError::UPLOAD_ABORTED);
        return;
    }
}

bool OtaUpdateServer::verifyPartition(const OtaPartitionRef* part, uint32_t size, const uint8_t* sig) {
    // Reject a null partition or an image larger than the partition's capacity.
    // Mirrors the inline ota_update.ino guard `size > part->size` exactly — the
    // real ESP32 OTA partition is typically 1.2-1.6MB, so the previous hardcoded
    // 1MB cap would reject legitimate firmware. The capacity comes from the
    // injected IPartition adapter (esp_partition_t.size on device).
    if (!part || size > partition_.size(part)) {
        return false;
    }

    // The Ed25519ph verify state is owned by the ICrypto adapter (gap 4a) — the
    // vanilla threads no state between these calls.
    if (crypto_.signEd25519phInit() != 0) {
        return false;
    }

    std::array<uint8_t, OtaConfig::VERIFY_CHUNK_SIZE> chunk;
    uint32_t off = 0;
    while (off < size) {
        size_t n = (size - off < OtaConfig::VERIFY_CHUNK_SIZE) ?
                   (size - off) : OtaConfig::VERIFY_CHUNK_SIZE;

        if (partition_.read(part, off, chunk.data(), n) != 0) {
            return false;
        }

        if (crypto_.signEd25519phUpdate(chunk.data(), n) != 0) {
            return false;
        }

        off += n;
        // WDT feed (gap 4c) lives in ArduinoCrypto::signEd25519phUpdate on the
        // same VERIFY_YIELD_INTERVAL cadence the inline used.
    }

    return crypto_.signEd25519phFinalVerify(sig, SENTINEL_PUBLIC_KEY) == 0;
}

// Testable pure functions

bool OtaUpdateServer::hexToByte(char c, uint8_t& v) {
    if (c >= '0' && c <= '9') { v = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
    return false;
}

bool OtaUpdateServer::parseHexSig(const std::string& hex, uint8_t* out) {
    if (hex.length() != 128) return false;
    for (size_t i = 0; i < 64; i++) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!hexToByte(hex[i*2], hi) || !hexToByte(hex[i*2+1], lo)) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string OtaUpdateServer::errorMessage(OtaError err) {
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

int OtaUpdateServer::httpCode(OtaError err) {
    switch (err) {
        case OtaError::NONE: return OtaConfig::HTTP_OK;
        case OtaError::SIGNATURE_VERIFY_FAILED:
        case OtaError::BAD_SIGNATURE_HEADER:
        case OtaError::SODIUM_NOT_READY:
            return OtaConfig::HTTP_FORBIDDEN;
        case OtaError::UPLOAD_TIMEOUT:
            return OtaConfig::HTTP_BAD_REQUEST;
        case OtaError::NO_SIGNATURE:
            return OtaConfig::HTTP_UNAUTHORIZED;
        default:
            return OtaConfig::HTTP_BAD_REQUEST;
    }
}

} // namespace esp32_firmware