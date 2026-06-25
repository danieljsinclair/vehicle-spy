#include "vehicle-sim/pipeline/OTAHttpTransport.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

using namespace vehicle_sim::pipeline;

// ── Base64 (RFC 4648) ──────────────────────────────────────────────────

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string OTAHttpTransport::base64Encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    uint8_t buf3[3];
    uint8_t buf4[4];
    for (unsigned char c : input) {
        buf3[i++] = c;
        if (i == 3) {
            buf4[0] = (buf3[0] & 0xFC) >> 2;
            buf4[1] = ((buf3[0] & 0x03) << 4) | ((buf3[1] & 0xF0) >> 4);
            buf4[2] = ((buf3[1] & 0x0F) << 2) | ((buf3[2] & 0xC0) >> 6);
            buf4[3] = buf3[2] & 0x3F;
            for (int j = 0; j < 4; j++) out += b64_table[buf4[j]];
            i = 0;
        }
    }
    if (i > 0) {
        for (size_t j = i; j < 3; j++) buf3[j] = 0;
        buf4[0] = (buf3[0] & 0xFC) >> 2;
        buf4[1] = ((buf3[0] & 0x03) << 4) | ((buf3[1] & 0xF0) >> 4);
        buf4[2] = ((buf3[1] & 0x0F) << 2) | ((buf3[2] & 0xC0) >> 6);
        for (size_t j = 0; j < i + 1; j++) out += b64_table[buf4[j]];
        while (i++ < 3) out += '=';
    }
    return out;
}

// ── Multipart body builder ───────────────────────────────────────────────
// Two parts: signature (64 bytes) then firmware (.bin).
// The device requires signature to arrive first.

std::string OTAHttpTransport::buildMultipartBody(
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& firmware,
        std::string& boundaryOut) {
    boundaryOut = "----OTABoundary7d2c4a9e1f";
    std::string body;

    // Part 1: signature field (must come first)
    body += "--" + boundaryOut + "\r\n";
    body += "Content-Disposition: form-data; name=\"signature\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body.append(reinterpret_cast<const char*>(signature.data()), signature.size());
    body += "\r\n";

    // Part 2: firmware field
    body += "--" + boundaryOut + "\r\n";
    body += "Content-Disposition: form-data; name=\"firmware\"; "
            "filename=\"firmware.bin\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body.append(reinterpret_cast<const char*>(firmware.data()), firmware.size());
    body += "\r\n";

    body += "--" + boundaryOut + "--\r\n";
    return body;
}

// ── Public API ───────────────────────────────────────────────────────────

OTAHttpTransport::OTAHttpTransport(std::string host, int port,
                                   std::string username, std::string password,
                                   int recvTimeoutMs)
    : host_(std::move(host))
    , port_(port)
    , username_(std::move(username))
    , password_(std::move(password))
    , recvTimeoutMs_(recvTimeoutMs) {}

OTAHttpTransport::~OTAHttpTransport() {
    if (fd_ >= 0) ::close(fd_);
}

bool OTAHttpTransport::open() {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        lastError_ = "socket() failed";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host_.c_str(), nullptr, &hints, &res) != 0 || !res) {
            lastError_ = "could not resolve host: " + host_;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        lastError_ = "connect() failed to " + host_ + ":" + std::to_string(port_);
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

bool OTAHttpTransport::isOpen() const noexcept {
    return fd_ >= 0;
}

bool OTAHttpTransport::push(const std::vector<uint8_t>& firmware,
                            const std::vector<uint8_t>& signature) {
    succeeded_ = false;
    lastError_.clear();

    if (fd_ < 0) {
        lastError_ = "not connected";
        return false;
    }

    if (signature.size() != 64) {
        lastError_ = "signature must be 64 bytes (Ed25519)";
        return false;
    }

    // Build multipart body: signature first, then firmware
    std::string boundary;
    std::string body = buildMultipartBody(signature, firmware, boundary);

    // Build Basic Auth token
    std::string credentials = username_ + ":" + password_;
    std::string authHeader = "Basic " + base64Encode(credentials);

    // Build HTTP request
    std::ostringstream req;
    req << "POST /update HTTP/1.1\r\n";
    req << "Host: " << host_ << "\r\n";
    req << "Authorization: " << authHeader << "\r\n";
    req << "Content-Type: multipart/form-data; boundary=" << boundary << "\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    req << "\r\n";
    req << body;

    std::string request = req.str();

    // Send request
    if (!sendAll(reinterpret_cast<const uint8_t*>(request.data()), request.size())) {
        lastError_ = "failed to send HTTP request";
        return false;
    }

    // Read response — uses the injected timeout (30s default; tests pass a
    // small value so the timeout path can be exercised without sleeping).
    std::string response = recvResponse(recvTimeoutMs_);
    if (response.empty()) {
        lastError_ = "no response from device (timeout)";
        return false;
    }

    // Parse HTTP status line
    auto pos = response.find("\r\n");
    if (pos == std::string::npos) {
        lastError_ = "malformed HTTP response";
        return false;
    }
    std::string statusLine = response.substr(0, pos);

    if (statusLine.find("200") != std::string::npos) {
        succeeded_ = true;
        return true;
    }

    // Extract error from body if present, otherwise use status line
    auto bodyStart = response.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        std::string respBody = response.substr(bodyStart + 4);
        lastError_ = respBody.empty() ? statusLine : respBody;
    } else {
        lastError_ = statusLine;
    }
    return false;
}

bool OTAHttpTransport::succeeded() const noexcept {
    return succeeded_;
}

const std::string& OTAHttpTransport::lastError() const noexcept {
    return lastError_;
}

// ── Internal helpers ─────────────────────────────────────────────────────

bool OTAHttpTransport::sendAll(const uint8_t* data, size_t len) noexcept {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd_, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string OTAHttpTransport::recvResponse(int timeoutMs) noexcept {
    std::string out;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    char buf[4096];  // Larger buffer for full response body
    bool headersComplete = false;

    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd_, &rs);
        // Poll for at most the time remaining until the deadline (capped at
        // 100ms), so a small timeout is actually honoured — otherwise select's
        // 100ms poll floor would round any sub-100ms timeout up to ~100ms.
        auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - std::chrono::steady_clock::now()).count();
        if (remainingMs <= 0) {
            // Timeout: distinguish from clean EOF
            if (!headersComplete) {
                // Timed out before headers complete
                return out;  // Empty or partial
            }
            break;  // Headers complete, timeout reading body is OK
        }
        const auto pollMs = std::min<decltype(remainingMs)>(remainingMs, 100);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = static_cast<suseconds_t>(pollMs * 1000);
        int r = select(fd_ + 1, &rs, nullptr, nullptr, &tv);
        if (r > 0) {
            ssize_t n = recv(fd_, buf, sizeof(buf), 0);
            if (n < 0) {
                // Error on socket
                break;
            }
            if (n == 0) {
                // Clean EOF from server
                break;
            }
            out.append(buf, static_cast<size_t>(n));
            if (!headersComplete && out.find("\r\n\r\n") != std::string::npos) {
                headersComplete = true;
            }
        } else if (r < 0 && errno != EINTR) {
            // Select error (not signal interrupt)
            break;
        }
    }

    // If we have headers but the device is sending a large error body,
    // try to drain the remainder briefly (devices may send detailed error messages)
    if (headersComplete) {
        // Drain any remaining bytes with a short timeout
        auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (std::chrono::steady_clock::now() < drainDeadline) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(fd_, &rs);
            timeval tv{};
            tv.tv_usec = 50 * 1000;  // 50ms poll
            int r = select(fd_ + 1, &rs, nullptr, nullptr, &tv);
            if (r > 0) {
                ssize_t n = recv(fd_, buf, sizeof(buf), 0);
                if (n <= 0) break;  // EOF or error
                out.append(buf, static_cast<size_t>(n));
            } else {
                break;  // Timeout or error
            }
        }
    }

    return out;
}
