#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

using namespace vehicle_sim::pipeline;

namespace {

// Minimal blocking TCP listener bound to an ephemeral localhost port. Accepts
// one client, can read what the client sends, and write canned responses.
class LoopbackServer {
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
    ~LoopbackServer() {
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

    std::string readClientBytes(std::size_t expectedMin, int timeoutMs) {
        std::string out;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        char buf[256];
        while (out.size() < expectedMin &&
               std::chrono::steady_clock::now() < deadline) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(clientFd_, &rs);
            timeval tv{};
            tv.tv_usec = 100 * 1000;
            int r = select(clientFd_ + 1, &rs, nullptr, nullptr, &tv);
            if (r > 0) {
                ssize_t n = recv(clientFd_, buf, sizeof(buf), 0);
                if (n <= 0) break;
                out.append(buf, static_cast<std::size_t>(n));
            }
        }
        return out;
    }

    // Read a single line (up to \r or \n) with timeout.
    std::string readLine(int timeoutMs = 3000) {
        std::string out;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        char c;
        while (std::chrono::steady_clock::now() < deadline) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(clientFd_, &rs);
            timeval tv{};
            tv.tv_usec = 100 * 1000;
            int r = select(clientFd_ + 1, &rs, nullptr, nullptr, &tv);
            if (r > 0) {
                ssize_t n = recv(clientFd_, &c, 1, 0);
                if (n <= 0) break;
                if (c == '\r' || c == '\n') {
                    if (!out.empty()) break;
                    continue;  // skip leading CR/LF
                }
                out += c;
            }
        }
        return out;
    }

    void sendBytes(const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            ssize_t n = ::send(clientFd_, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) break;
            sent += static_cast<std::size_t>(n);
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

// Helper: accept a connection, read the AUTH line, send "OK". Returns true if
// auth succeeded.
bool acceptAndAuth(LoopbackServer& server, int timeoutMs = 3000) {
    if (server.acceptClient(timeoutMs) < 0) return false;
    std::string line = server.readLine(timeoutMs);
    if (line != "AUTH vehicle-sim-2026") return false;
    server.sendBytes("OK\r");
    return true;
}

// ============================================================
// RAW protocol (default) — AUTH token sent on connect, then stream verbatim.
// ============================================================

TEST(TCPTransportTest, RawProtocol_SendsAuthOnConnect) {
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    // Start the transport open in a thread so we can accept+auth before it times out.
    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), /*adapterProtocol=*/"raw");
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    // Accept and auth before checking open result.
    ASSERT_TRUE(acceptAndAuth(server));
    th.join();
    ASSERT_TRUE(opened.load());

    // After AUTH, RAW sends nothing — the bridge streams first.
    std::string sent = server.readClientBytes(1, 300);
    EXPECT_TRUE(sent.empty())
        << "raw TCP must not send AT-init after auth, but sent: " << sent;

    server.closeClient();
    TCPTransport::requestStop();
    EXPECT_FALSE(t.nextLine().has_value());
    TCPTransport::resetStop();
}

TEST(TCPTransportTest, RawProtocol_ParsesFrameLinesThroughNormaliser) {
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), "raw");
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    ASSERT_TRUE(acceptAndAuth(server));
    th.join();
    ASSERT_TRUE(opened.load());

    // Feed a couple of raw CAN-monitor lines (HEX CAN IDs).
    server.sendBytes("118 3C 00 18 00 00 00 00 FF\r");
    server.sendBytes("108 00 00 00 90 01 00 00 00\r");

    RawFrameNormaliser n;
    std::vector<std::uint32_t> ids;
    while (auto line = t.nextLine()) {
        auto r = n.normalise(*line);
        if (r.kind == NormaliserResultKind::Frame) {
            ids.push_back(r.frame.canId);
        }
        if (ids.size() >= 2) break;
    }
    ASSERT_GE(ids.size(), 2u);
    EXPECT_EQ(ids[0], 0x118u);
    EXPECT_EQ(ids[1], 0x108u);

    server.closeClient();
    TCPTransport::requestStop();
    t.nextLine();
    TCPTransport::resetStop();
}

TEST(TCPTransportTest, CleanDisconnect_DetectedByNextLine) {
    // Verify that when the server closes, nextLine() detects the disconnect
    // (returns nullopt) without hanging. The transport attempts reconnect in
    // the background; we stop it with requestStop().
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), "raw");
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    ASSERT_TRUE(acceptAndAuth(server));
    th.join();
    ASSERT_TRUE(opened.load());

    // Send one frame.
    server.sendBytes("118 3C 00 18 00 00 00 00 FF\r");
    auto line = t.nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "118 3C 00 18 00 00 00 00 FF");

    // Server closes — transport detects disconnect and attempts reconnect.
    server.closeClient();

    // The transport should eventually return nullopt (either from EOF detection
    // or from reconnect failure). Use requestStop to bound the wait.
    TCPTransport::requestStop();
    line = t.nextLine();
    EXPECT_FALSE(line.has_value());
    TCPTransport::resetStop();
}

TEST(TCPTransportTest, ConnectionRefused_OpenReturnsFalse) {
    // Bind then close a port so connect() is refused.
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

    TCPTransport t("127.0.0.1", port, "raw");
    EXPECT_FALSE(t.open());
    EXPECT_FALSE(t.isOpen());
}

// ============================================================
// ELM327 protocol — AUTH + AT-init on connect.
// ============================================================

TEST(TCPTransportTest, Elm327Protocol_SendsAuthThenAtInitOnConnect) {
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), /*adapterProtocol=*/"elm327");
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    ASSERT_TRUE(acceptAndAuth(server));
    th.join();
    ASSERT_TRUE(opened.load());

    // After AUTH, ELM327 must send the AT-init sequence (ATZ/ATE0/ATSP6/ATH1/ATMA).
    std::string sent = server.readClientBytes(
        std::string("ATZ\rATE0\rATSP6\rATH1\rATMA\r").size(), 2000);
    EXPECT_NE(sent.find("ATZ\r"), std::string::npos);
    EXPECT_NE(sent.find("ATE0\r"), std::string::npos);
    EXPECT_NE(sent.find("ATSP6\r"), std::string::npos);
    EXPECT_NE(sent.find("ATH1\r"), std::string::npos);
    EXPECT_NE(sent.find("ATMA\r"), std::string::npos);

    server.closeClient();
    TCPTransport::requestStop();
    t.nextLine();
    TCPTransport::resetStop();
}

TEST(TCPTransportTest, Elm327InitFailure_OpenReturnsFalse) {
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), "elm327");
    // Open the transport; it will start sending AUTH then AT-init.
    // Close the server-side client immediately so the send fails.
    std::atomic<bool> opened{false};
    std::thread th([&] {
        opened = t.open();
    });
    ASSERT_GE(server.acceptClient(), 0);
    // Read and discard the AUTH line, then close before sending OK.
    server.readLine();
    server.closeClient();
    th.join();
    // open() should fail because the auth handshake was interrupted.
    EXPECT_FALSE(opened.load());
    TCPTransport::resetStop();
}

TEST(TCPTransportTest, RequestStop_TerminatesNextLine) {
    // A quiet peer + requestStop() must make nextLine() return nullopt
    // promptly (this is how Ctrl+C stops a live stream).
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), "raw");
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    ASSERT_TRUE(acceptAndAuth(server));
    th.join();
    ASSERT_TRUE(opened.load());

    // Request stop from another "thread" (simulating a signal handler).
    TCPTransport::requestStop();
    // nextLine should return nullopt within ~one select timeout (0.5s).
    auto start = std::chrono::steady_clock::now();
    auto r = t.nextLine();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    EXPECT_FALSE(r.has_value());
    EXPECT_LT(elapsed.count(), 1500) << "stop should be prompt";

    server.closeClient();
    TCPTransport::resetStop();
}

TEST(TCPTransportTest, AuthRejected_OpenReturnsFalse) {
    // Server that accepts the connection but sends "ERROR" instead of "OK".
    LoopbackServer server;
    ASSERT_TRUE(server.init());

    TCPTransport::resetStop();
    TCPTransport t("127.0.0.1", server.port(), "raw");
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });

    ASSERT_GE(server.acceptClient(), 0);
    // Read the AUTH line.
    std::string line = server.readLine();
    ASSERT_EQ(line, "AUTH vehicle-sim-2026");
    // Send rejection instead of OK.
    server.sendBytes("ERROR unauthorized\r");
    th.join();
    EXPECT_FALSE(opened.load());
    EXPECT_FALSE(t.isOpen());
    server.closeClient();
    TCPTransport::resetStop();
}

} // namespace vehicle_sim::pipeline
