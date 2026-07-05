#include "OtaUpdateServer.h"
#include <cstring>

namespace esp32_firmware {

// External public key (baked in)
const uint8_t OTA_PUBLIC_KEY[32] = {0};  // Will be replaced at build time

OtaUpdateServer::OtaUpdateServer(IHttpServer& http, IHttpUpdateServer& updater,
                                 IUpdate& update, IPartition& partition, ICrypto& crypto)
    : http_(http), updater_(updater), update_(update), partition_(partition), crypto_(crypto) {}

void OtaUpdateServer::setup() {
    if (crypto_.sodiumInit() >= 0) {
        sodiumReady_ = true;
    }

    static const char* hdrs[1] = { OtaConfig::OTA_SIG_HDR };
    http_.collectHeaders(hdrs, 1);

    updater_.setup(&http_, "/update", "", "");

    http_.on("/update", 1, [this]() { handleGet(); });  // HTTP_GET
    http_.on("/update", 2, [this]() { handlePost(); }, [this]() { handleUpload(); });  // HTTP_POST

    http_.begin();
}

void OtaUpdateServer::loop() {
    http_.handleClient();
}

void OtaUpdateServer::markValidOnBoot() {
    const void* running = partition_.getRunningPartition();
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

void OtaUpdateServer::handleUpload() {
    // In the real implementation, this gets the upload from http_.upload()
    // For testing, we'll simulate the upload handling
    // This is a simplified version - the real one uses HTTPUpload& up = http_.upload()
}

bool OtaUpdateServer::verifyPartition(const void* part, uint32_t size, const uint8_t* sig) {
    if (!part || size > 1024 * 1024) {  // Max 1MB for testing
        return false;
    }

    // Mock state for crypto
    void* state = nullptr;
    if (crypto_.signEd25519phInit(state) != 0) {
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

        if (crypto_.signEd25519phUpdate(state, chunk.data(), n) != 0) {
            return false;
        }

        off += n;

        // Feed WDT periodically
        if ((off & OtaConfig::VERIFY_YIELD_INTERVAL) == 0) {
            // In real implementation: delay(1);
        }
    }

    return crypto_.signEd25519phFinalVerify(state, sig, OTA_PUBLIC_KEY) == 0;
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