#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vehicle_sim::pipeline {

/**
 * OTA HTTP transport: pushes a signed firmware binary to an ESP32 running
 * the custom OTA HTTP server (port 80, /update) via HTTP POST
 * multipart/form-data with Basic Auth.
 *
 * The firmware image is accompanied by a 64-byte Ed25519ph detached signature.
 * The ESP32 verifies the signature before committing the update.
 *
 * This is a ONE-SHOT transport: open() connects, push() sends the signed
 * firmware, and the result is available via lastError() / succeeded().
 *
 * Protocol: HTTP POST /update with multipart/form-data body containing two fields:
 *   1. "signature" (64 bytes, Ed25519ph detached signature)
 *   2. "firmware"  (raw .bin)
 * Authentication via HTTP Basic Auth.
 */
class OTAHttpTransport {
public:
    /**
     * @param host           IPv4 address or hostname of the ESP32.
     * @param port           HTTP port (default 80).
     * @param username       HTTP Basic Auth username.
     * @param password       HTTP Basic Auth password.
     * @param recvTimeoutMs  Max wait (ms) for the device's HTTP response.
     *                       Defaults to 30s — OTA signature verification can
     *                       take time. Injectable so tests can use a tiny value
     *                       instead of sleeping.
     */
    OTAHttpTransport(std::string host, int port,
                     std::string username, std::string password,
                     int recvTimeoutMs = 30000);

    ~OTAHttpTransport();

    OTAHttpTransport(const OTAHttpTransport&) = delete;
    OTAHttpTransport& operator=(const OTAHttpTransport&) = delete;
    OTAHttpTransport(OTAHttpTransport&&) = delete;
    OTAHttpTransport& operator=(OTAHttpTransport&&) = delete;

    /** Open a TCP connection to the ESP32. */
    bool open();

    /** True if the TCP connection is open. */
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * Push a signed firmware binary to the ESP32.
     *
     * @param firmware  Raw firmware bytes (the .bin to flash).
     * @param signature 64-byte Ed25519ph detached signature over the firmware.
     * @return true if the server accepted the update (HTTP 200).
     */
    bool push(const std::vector<uint8_t>& firmware,
              const std::vector<uint8_t>& signature);

    /** True if the last push() succeeded. */
    [[nodiscard]] bool succeeded() const noexcept;

    /** Error message from the last operation, or empty on success. */
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    bool sendAll(const uint8_t* data, size_t len) const noexcept;
    std::string recvResponse(int timeoutMs) const noexcept;
    static std::string base64Encode(const std::string& input);
    static std::string buildMultipartBody(const std::vector<uint8_t>& signature,
                                          const std::vector<uint8_t>& firmware,
                                          std::string& boundaryOut);

    std::string host_;
    int port_;
    std::string username_;
    std::string password_;
    int recvTimeoutMs_ = 30000;
    int fd_ = -1;
    bool succeeded_ = false;
    std::string lastError_;
};

} // namespace vehicle_sim::pipeline
