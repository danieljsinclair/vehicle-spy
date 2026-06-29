#include "vehicle-sim/pipeline/OTAHttpTransport.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>

using namespace vehicle_sim::pipeline;

// ── Base64 (RFC 4648) ──────────────────────────────────────────────────

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

OTAHttpTransport::OTAHttpTransport(std::string host, int port,
                                   std::string username, std::string password,
                                   int recvTimeoutMs)
    : host_(std::move(host))
    , port_(port)
    , username_(std::move(username))
    , password_(std::move(password))
    , recvTimeoutMs_(recvTimeoutMs) {}

OTAHttpTransport::~OTAHttpTransport() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

std::string OTAHttpTransport::base64Encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    std::array<std::byte, 3> buf3{};
    std::array<std::byte, 4> buf4{};
    for (unsigned char c : input) {
        buf3[i++] = std::byte{c};
        if (i == 3) {
            buf4[0] = (buf3[0] & std::byte{0xFC}) >> 2;
            buf4[1] = ((buf3[0] & std::byte{0x03}) << 4) | ((buf3[1] & std::byte{0xF0}) >> 4);
            buf4[2] = ((buf3[1] & std::byte{0x0F}) << 2) | ((buf3[2] & std::byte{0xC0}) >> 6);
            buf4[3] = buf3[2] & std::byte{0x3F};
            for (int j = 0; j < 4; j++) out += b64_table[std::to_integer<unsigned int>(buf4[j])];
            i = 0;
        }
    }
    if (i > 0) {
        for (size_t j = i; j < 3; j++) buf3[j] = std::byte{0};
        buf4[0] = (buf3[0] & std::byte{0xFC}) >> 2;
        buf4[1] = ((buf3[0] & std::byte{0x03}) << 4) | ((buf3[1] & std::byte{0xF0}) >> 4);
        buf4[2] = ((buf3[1] & std::byte{0x0F}) << 2) | ((buf3[2] & std::byte{0xC0}) >> 6);
        for (size_t j = 0; j < i + 1; j++) out += b64_table[std::to_integer<unsigned int>(buf4[j])];
        while (i++ < 3) out += '=';
    }
    return out;
}

std::string OTAHttpTransport::buildMultipartBody(
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& firmware,
        std::string& boundaryOut) {
    boundaryOut = "----OTABoundary7d2c4a9e1f";
    std::string body;

    // Part 1: signature
    body += "--" + boundaryOut + "\r\n";
    body += "Content-Disposition: form-data; name=\"signature\"; filename=\"signature.bin\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body.append(reinterpret_cast<const char*>(signature.data()), signature.size());
    body += "\r\n";

    // Part 2: firmware
    body += "--" + boundaryOut + "\r\n";
    body += "Content-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body.append(reinterpret_cast<const char*>(firmware.data()), firmware.size());
    body += "\r\n";

    // End boundary
    body += "--" + boundaryOut + "--\r\n";

    return body;
}

bool OTAHttpTransport::sendAll(const uint8_t* data, size_t len) noexcept {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd_, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool OTAHttpTransport::open() {
    // Resolve host
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(port_);
    addrinfo* res = nullptr;
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0) {
        lastError_ = "DNS resolution failed";
        return false;
    }

    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd_ < 0) continue;
#ifdef SO_NOSIGPIPE
        int nosig = 1;
        (void)setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif
        struct timeval tv{};
        tv.tv_sec = static_cast<time_t>(recvTimeoutMs_ / 1000);
        tv.tv_usec = static_cast<suseconds_t>((recvTimeoutMs_ % 1000) * 1000);
        (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(fd_, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(res);
    if (fd_ < 0) {
        lastError_ = "Connection failed";
        return false;
    }
    return true;
}

bool OTAHttpTransport::push(const std::vector<uint8_t>& firmware,
                            const std::vector<uint8_t>& signature) {
    if (fd_ < 0) {
        lastError_ = "not connected";
        return false;
    }

    if (signature.size() != 64) {
        lastError_ = "signature must be 64 bytes";
        return false;
    }

    std::string boundary;
    std::string body = buildMultipartBody(signature, firmware, boundary);

    // Build HTTP Basic Auth header
    std::string auth = username_ + ":" + password_;
    std::string authEncoded = base64Encode(auth);

    // Build HTTP request
    std::ostringstream req;
    req << "POST /update HTTP/1.1\r\n";
    req << "Host: " << host_ << "\r\n";
    req << "Authorization: Basic " << authEncoded << "\r\n";
    req << "Content-Type: multipart/form-data; boundary=" << boundary << "\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    req << "\r\n";
    req << body;
    std::string request = req.str();

    // Send request
    if (!sendAll(reinterpret_cast<const uint8_t*>(request.data()), request.size())) {
        lastError_ = "Send failed";
        return false;
    }

    // Read response
    std::string response = recvResponse(recvTimeoutMs_);

    // Parse HTTP status
    succeeded_ = response.find("200") != std::string::npos;
    if (!succeeded_) {
        lastError_ = "Server response: " + response.substr(0, 200);
    }
    return succeeded_;
}

bool OTAHttpTransport::isOpen() const noexcept {
    return fd_ >= 0;
}

bool OTAHttpTransport::succeeded() const noexcept {
    return succeeded_;
}

const std::string& OTAHttpTransport::lastError() const noexcept {
    return lastError_;
}

std::string OTAHttpTransport::recvResponse(int timeoutMs) noexcept {
    std::string response;
    char buffer[4096];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd_, &rs);
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        timeval tv{};
        tv.tv_sec = static_cast<time_t>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<suseconds_t>(remaining.count() % 1000000);
        int r = select(fd_ + 1, &rs, nullptr, nullptr, &tv);
        if (r > 0) {
            ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            response.append(buffer, static_cast<size_t>(n));
        } else if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
    }
    return response;
}