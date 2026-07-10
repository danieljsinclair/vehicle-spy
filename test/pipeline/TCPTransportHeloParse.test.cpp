#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using namespace vehicle_sim::pipeline;

// ============================================================================
// SOCKET-FREE-NET SAFETY NET for the S3776 (cc=35) candidate
// `TCPTransport::sendHeloAndParseAck` (TCPTransport.cpp:214-338).
//
// That method does two things: (1) socket I/O with a live device (ATI / ATHELO
// + recv loop), and (2) PURE, DETERMINISTIC, SOCKET-FREE parsing: validate the
// ACK string shape, extract the 32-hex device id, hex-decode it to 16 bytes.
//
// We cannot call sendHeloAndParseAck() directly (it needs a live fd_), and the
// 2-char hex decoder `parseHexByte` is an anonymous-namespace free function with
// internal linkage (not linkable from a test binary). So we drive the REAL parse
// through the one honest, deterministic seam that already exists in the suite:
// a loopback TCP server whose ACK line we fully control. open() performs the
// genuine HELO handshake (real socket I/O + real validation code), and the
// observable OUTCOME is surfaced via getDeviceId() (which re-encodes the parsed
// 16 bytes via "%02X") plus open()/isOpen() success-or-failure.
//
// These are CHARACTERISATION tests: they lock the externally-observable contract
// of the validation (length = exactly 32 hex, hex-decode, CRLF/prompt strip,
// uppercase canonicalisation) so the upcoming S3776 extract-and-simplify refactor
// cannot silently change parse semantics.
//
// GAP (flagged for the refactor agent, NOT hacked here): the production code
// declares but never uses the `FIRMWARE=` token — an ACK is accepted as long as
// it contains "ACK DEVICE=" and "DEVICEID=<32hex>". We deliberately do NOT
// assert "FIRMWARE= is required"; we only assert what the current code enforces.
// ============================================================================

namespace {

// Minimal blocking TCP listener bound to an ephemeral localhost port. Accept one
// client, speak the AUTH/ATI/ATHELO handshake, then emit a caller-supplied ACK.
// Self-contained (no symbols shared with TCPTransport.test.cpp).
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

    // Read a single line (up to \r or \n), skipping leading CR/LF.
    std::string readLine(int timeoutMs = 3000) {
        std::string out;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        char c;
        while (std::chrono::steady_clock::now() < deadline) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(clientFd_, &rs);
            auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now()).count();
            if (remainingMs <= 0) break;
            timeval tv{};
            tv.tv_usec = static_cast<suseconds_t>(
                std::min<decltype(remainingMs)>(remainingMs, 100) * 1000);
            int r = select(clientFd_ + 1, &rs, nullptr, nullptr, &tv);
            if (r > 0) {
                ssize_t n = recv(clientFd_, &c, 1, 0);
                if (n <= 0) break;
                if (c == '\r' || c == '\n') {
                    if (!out.empty()) break;
                    continue;
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

// Result of one controlled HELO handshake.
struct HeloResult {
    bool opened = false;
    std::string deviceId;  // getDeviceId() after open()
    std::unique_ptr<TCPTransport> transport;
    std::shared_ptr<StopToken> stop;
};

// Speak AUTH/ATI/ATHELO, then emit `ack` (the raw ACK bytes, including any
// trailing terminator the test wants). If closeAfterAck is true the server closes
// the client socket immediately after sending, which makes the client's recv()
// loop terminate promptly even when the ACK lacks a "\r\r>" terminator (so
// rejection cases don't stall on the 500ms SO_RCVTIMEO).
static HeloResult runHeloHandshake(const std::string& ack, bool closeAfterAck) {
    HeloResult r;
    r.stop = std::make_shared<StopToken>();

    LoopbackServer server;
    if (!server.init()) return r;

    r.transport = std::make_unique<TCPTransport>(
        "127.0.0.1", server.port(), /*adapterProtocol=*/"raw",
        std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 500}, r.stop);

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = r.transport->open(); });

    if (server.acceptClient() < 0) { th.join(); return r; }

    // AUTH
    if (server.readLine() != "AUTH vehicle-sim-2026") { th.join(); return r; }
    server.sendBytes("OK\r");

    // ATI
    if (server.readLine() != "ATI") { th.join(); return r; }
    server.sendBytes("ESP32 CAN Bridge v0.1\r>");

    // ATHELO -> our controlled ACK
    if (server.readLine() != "ATHELO") { th.join(); return r; }
    server.sendBytes(ack);
    if (closeAfterAck) server.closeClient();

    th.join();
    r.opened = opened.load();
    if (r.opened) {
        r.deviceId = r.transport->getDeviceId();
    }
    return r;
}

// Tidy teardown helper: stop the transport and drain one line.
static void finish(HeloResult& r) {
    if (r.stop) r.stop->requestStop();
    if (r.transport) r.transport->nextLine();
    if (r.stop) r.stop->reset();
}

// Well-formed ACK prefix shared by the happy-path cases.
constexpr const char* kAckHead = "ACK DEVICE=ESP32-CAN FIRMWARE=0.1 DEVICEID=";

} // namespace

// --- HAPPY PATH: valid 32-hex decodes to the exact byte sequence ---------------

TEST(TCPTransportHeloParseContract, Valid32Hex_RoundTripsToDeviceId) {
    // Canonical known vector: 00112233445566778899AABBCCDDEEFF must decode to
    // exactly those 16 bytes (and be re-emitted unchanged by "%02X").
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "\r\r>", false);
    EXPECT_TRUE(r.opened);
    EXPECT_TRUE(r.transport && r.transport->isOpen());
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

TEST(TCPTransportHeloParseContract, Valid32Hex_AllFfAndAllZero) {
    // Boundary values within a valid length: all zeroes and all 0xFF both parse.
    {
        HeloResult r = runHeloHandshake(
            std::string(kAckHead) + "00000000000000000000000000000000" + "\r\r>", false);
        EXPECT_TRUE(r.opened);
        EXPECT_EQ(r.deviceId, "00000000000000000000000000000000");
        finish(r);
    }
    {
        HeloResult r = runHeloHandshake(
            std::string(kAckHead) + "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF" + "\r\r>", false);
        EXPECT_TRUE(r.opened);
        EXPECT_EQ(r.deviceId, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        finish(r);
    }
}

// --- CASE-INSENSITIVE hex, canonicalised to UPPERCASE --------------------------

TEST(TCPTransportHeloParseContract, LowercaseHex_AcceptedAndCanonicalisedToUpper) {
    // The decoder accepts a-f; performHeloHandshake re-emits via "%02X" (upper),
    // so lowercase input must surface as the uppercase device id.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899aabbccddeeff" + "\r\r>", false);
    EXPECT_TRUE(r.opened);
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

// --- LENGTH BOUNDARY: must be EXACTLY 32 hex chars -----------------------------

TEST(TCPTransportHeloParseContract, ThirtyOneHex_Rejected) {
    // One short of the required 32 -> handshake must fail.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "0" + "\r\r>", true);
    EXPECT_FALSE(r.opened);
    EXPECT_TRUE(r.transport && !r.transport->isOpen());
    finish(r);
}

TEST(TCPTransportHeloParseContract, ThirtyThreeHex_Rejected) {
    // One too many -> handshake must fail.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "00" + "\r\r>", true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

// --- MALFORMED: non-hex characters are rejected --------------------------------

TEST(TCPTransportHeloParseContract, NonHexChars_Rejected) {
    // 'G' and 'Z' are not valid hex; the per-byte parse must fail.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEZZ" + "\r\r>", true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

// --- MISSING REQUIRED TOKENS --------------------------------------------------

TEST(TCPTransportHeloParseContract, MissingDeviceIdToken_Rejected) {
    // An ACK with no "DEVICEID=" field must not be accepted.
    HeloResult r = runHeloHandshake(
        "ACK DEVICE=ESP32-CAN FIRMWARE=0.1\r\r>", true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

TEST(TCPTransportHeloParseContract, MissingAckDevicePrefix_Rejected) {
    // The producer must emit the "ACK DEVICE=" prefix; a bare DEVICEID line is
    // not a valid ACK.
    HeloResult r = runHeloHandshake(
        "DEVICEID=00112233445566778899AABBCCDDEEFF\r\r>", true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

// --- TRAILING WHITESPACE / CRLF / PROMPT STRIPPING -----------------------------

TEST(TCPTransportHeloParseContract, TrailingSpaceStripped) {
    // A single trailing space before the prompt is stripped.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + " \r\r>", false);
    EXPECT_TRUE(r.opened);
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

TEST(TCPTransportHeloParseContract, TrailingNewlineStripped) {
    // A trailing '\n' is stripped, leaving a clean 32-hex id.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "\n", false);
    EXPECT_TRUE(r.opened);
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}
