#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/OTAHttpTransport.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;

namespace {

// Minimal blocking HTTP listener on an ephemeral localhost port. Accepts one
// client, reads the full HTTP request, and writes a canned HTTP response.
class HttpLoopbackServer {
public:
    bool init() {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) return false;
        int yes = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (listen(listenFd_, 1) != 0) return false;
        socklen_t len = sizeof(addr);
        if (getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        port_ = ntohs(addr.sin_port);
        return port_ > 0;
    }

    ~HttpLoopbackServer() {
        if (clientFd_ >= 0) close(clientFd_);
        if (listenFd_ >= 0) close(listenFd_);
    }

    int port() const { return port_; }

    int acceptClient(int timeoutMs = 3000) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(listenFd_, &rs);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int r = select(listenFd_ + 1, &rs, nullptr, nullptr, &tv);
        if (r <= 0) { clientFd_ = -1; return -1; }
        clientFd_ = accept(listenFd_, nullptr, nullptr);
        return clientFd_;
    }

    // Read the full HTTP request (headers + body).
    std::string readRequest(int timeoutMs = 3000) {
        std::string req;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        char buf[1024];
        while (std::chrono::steady_clock::now() < deadline) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(clientFd_, &rs);
            timeval tv{};
            tv.tv_usec = 50 * 1000;
            int r = select(clientFd_ + 1, &rs, nullptr, nullptr, &tv);
            if (r > 0) {
                ssize_t n = recv(clientFd_, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, static_cast<size_t>(n));
                auto hdrEnd = req.find("\r\n\r\n");
                if (hdrEnd != std::string::npos) {
                    auto clPos = req.find("Content-Length:");
                    if (clPos != std::string::npos && clPos < hdrEnd) {
                        size_t clValStart = clPos + 15;
                        size_t clValEnd = req.find("\r\n", clValStart);
                        size_t contentLen = static_cast<size_t>(
                            std::stoul(req.substr(clValStart, clValEnd - clValStart)));
                        size_t bodyStart = hdrEnd + 4;
                        if (req.size() >= bodyStart + contentLen) break;
                    } else {
                        break;
                    }
                }
            }
        }
        return req;
    }

    void sendResponse(const std::string& response) {
        size_t sent = 0;
        while (sent < response.size()) {
            ssize_t n = ::send(clientFd_, response.data() + sent,
                               response.size() - sent, 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
    }

    void closeClient() {
        if (clientFd_ >= 0) { close(clientFd_); clientFd_ = -1; }
    }

private:
    int listenFd_ = -1;
    int clientFd_ = -1;
    int port_ = 0;
};

// Helper: build a 64-byte fake signature
static std::vector<uint8_t> fakeSignature() {
    std::vector<uint8_t> sig(64, 0xAB);
    return sig;
}

// ── OTAHttpTransport tests ────────────────────────────────────────────────

TEST(OTAHttpTransportTest, PushSendsHttpPost) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "vehicle-sim");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware = {0x01, 0x02, 0x03, 0x04};
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        std::string req = server.readRequest();
        server.sendResponse(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 24\r\n"
            "\r\n"
            "Update Success! Rebooting...");
        server.closeClient();
    });

    bool result = transport.push(firmware, sig);
    serverThread.join();

    EXPECT_TRUE(result);
    EXPECT_TRUE(transport.succeeded());
    EXPECT_TRUE(transport.lastError().empty());
}

TEST(OTAHttpTransportTest, PushReceivesHttp200) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware(256, 0xAB);
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        server.readRequest();
        server.sendResponse(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 0\r\n"
            "\r\n");
        server.closeClient();
    });

    EXPECT_TRUE(transport.push(firmware, sig));
    serverThread.join();
}

TEST(OTAHttpTransportTest, PushServerErrorSetsError) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware = {0x01};
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        server.readRequest();
        server.sendResponse(
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "bad signature");
        server.closeClient();
    });

    bool result = transport.push(firmware, sig);
    serverThread.join();

    EXPECT_FALSE(result);
    EXPECT_FALSE(transport.succeeded());
    EXPECT_NE(transport.lastError().find("bad signature"), std::string::npos)
        << "Error should contain the server's error message, got: " << transport.lastError();
}

TEST(OTAHttpTransportTest, ConnectionRefused_OpenReturnsFalse) {
    int refuseFd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(refuseFd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int yes = 1;
    setsockopt(refuseFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ASSERT_EQ(bind(refuseFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(refuseFd, 1), 0);
    socklen_t len = sizeof(addr);
    getsockname(refuseFd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    close(refuseFd);

    OTAHttpTransport transport("127.0.0.1", port, "ota", "pass");
    EXPECT_FALSE(transport.open());
    EXPECT_FALSE(transport.isOpen());
    EXPECT_FALSE(transport.lastError().empty());
}

TEST(OTAHttpTransportTest, RequestContainsAuthorizationHeader) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "vehicle-sim");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware = {0x01, 0x02};
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        std::string req = server.readRequest();
        EXPECT_NE(req.find("Authorization: Basic"), std::string::npos)
            << "HTTP request must contain Authorization header";
        EXPECT_NE(req.find("Authorization: Basic b3RhOnZlaGljbGUtc2lt"), std::string::npos)
            << "Authorization header must contain base64-encoded credentials";

        server.sendResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        server.closeClient();
    });

    transport.push(firmware, sig);
    serverThread.join();
}

TEST(OTAHttpTransportTest, RequestContainsMultipartSignatureAndFirmware) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware = {0xDE, 0xAD, 0xBE, 0xEF};
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        std::string req = server.readRequest();
        // Check multipart/form-data content type
        EXPECT_NE(req.find("Content-Type: multipart/form-data"), std::string::npos)
            << "Request must be multipart/form-data";
        // Check signature field comes first
        auto sigPos = req.find("name=\"signature\"");
        auto fwPos = req.find("name=\"firmware\"");
        EXPECT_NE(sigPos, std::string::npos) << "Multipart must contain 'signature' field";
        EXPECT_NE(fwPos, std::string::npos) << "Multipart must contain 'firmware' field";
        EXPECT_LT(sigPos, fwPos) << "Signature field must come before firmware field";
        // Check firmware bytes are in the body
        EXPECT_NE(req.find(std::string(reinterpret_cast<const char*>(firmware.data()),
                                       firmware.size())),
                  std::string::npos)
            << "Firmware bytes must appear in the request body";

        server.sendResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        server.closeClient();
    });

    transport.push(firmware, sig);
    serverThread.join();
}

TEST(OTAHttpTransportTest, RequestTargetIsUpdateEndpoint) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware = {0x01};
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        std::string req = server.readRequest();
        EXPECT_NE(req.find("POST /update HTTP/1.1"), std::string::npos)
            << "Request must target /update endpoint";

        server.sendResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        server.closeClient();
    });

    transport.push(firmware, sig);
    serverThread.join();
}

TEST(OTAHttpTransportTest, PushWithoutOpen_Fails) {
    OTAHttpTransport transport("127.0.0.1", 80, "ota", "pass");
    std::vector<uint8_t> firmware = {0x01};
    auto sig = fakeSignature();
    EXPECT_FALSE(transport.push(firmware, sig));
    EXPECT_FALSE(transport.succeeded());
    EXPECT_EQ(transport.lastError(), "not connected");
}

TEST(OTAHttpTransportTest, PushWithWrongSignatureSize_Fails) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());

    std::vector<uint8_t> firmware = {0x01};
    std::vector<uint8_t> badSig(32, 0xAB);  // Wrong size — must be 64
    EXPECT_FALSE(transport.push(firmware, badSig));
    EXPECT_FALSE(transport.succeeded());
    EXPECT_NE(transport.lastError().find("64 bytes"), std::string::npos);
}

TEST(OTAHttpTransportTest, ServerTimeout_ReturnsError) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware = {0x01};
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        server.readRequest();
        std::this_thread::sleep_for(std::chrono::milliseconds(20000));
        server.closeClient();
    });

    bool result = transport.push(firmware, sig);
    serverThread.join();

    EXPECT_FALSE(result);
    EXPECT_FALSE(transport.succeeded());
    EXPECT_FALSE(transport.lastError().empty());
}

TEST(OTAHttpTransportTest, LargeFirmwarePush) {
    HttpLoopbackServer server;
    ASSERT_TRUE(server.init());

    OTAHttpTransport transport("127.0.0.1", server.port(), "ota", "pass");
    ASSERT_TRUE(transport.open());
    ASSERT_GE(server.acceptClient(), 0);

    std::vector<uint8_t> firmware(65536, 0x42);
    auto sig = fakeSignature();
    std::thread serverThread([&] {
        std::string req = server.readRequest();
        EXPECT_GT(req.size(), firmware.size())
            << "Request must contain the full firmware plus headers";

        server.sendResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        server.closeClient();
    });

    bool result = transport.push(firmware, sig);
    serverThread.join();

    EXPECT_TRUE(result);
    EXPECT_TRUE(transport.succeeded());
}

} // namespace
