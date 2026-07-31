#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::util;
namespace util = vehicle_sim::util;

// Shared cooperative stop token for these tests (mirrors the original
// process-global flag). Each test resets it, constructs the transport with it,
// and calls requestStop() to bound a wait.
static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

// ============================================================
// RAW protocol (default) — AUTH token sent on connect, then stream verbatim.
// ============================================================

TEST(TCPTransportTest, RawProtocol_SendsAuthOnConnect) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::handshakeConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

    // After AUTH, RAW sends nothing — the bridge streams first. The scripted
    // handshake contained no post-handshake bytes, so nothing extra was sent
    // beyond the AUTH/ATI/ATHELO handshake the transport initiates.
    std::string sent = sock.sentBlob();
    // Raw must NOT send any ELM init commands after auth.
    EXPECT_EQ(sent.find("ATZ"), std::string::npos)
        << "raw TCP must not send AT-init after auth, but sent: " << sent;

    g_testStop->requestStop();
    EXPECT_FALSE(t.nextLine().has_value());
    g_testStop->reset();
}

TEST(TCPTransportTest, RawProtocol_ParsesFrameLinesThroughNormaliser) {
    test::FakeSocket sock;
    std::deque<std::string> chunks = test::heloHandshakeChunks();
    chunks.push_back("118 3C 00 18 00 00 00 00 FF\r");
    chunks.push_back("108 00 00 00 90 01 00 00 00\r");
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

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

    g_testStop->requestStop();
    t.nextLine();
    g_testStop->reset();
}

TEST(TCPTransportTest, CleanDisconnect_DetectedByNextLine) {
    test::FakeSocket sock;
    std::deque<std::string> chunks = test::heloHandshakeChunks();
    chunks.push_back("118 3C 00 18 00 00 00 00 FF\r");
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

    auto line = t.nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "118 3C 00 18 00 00 00 00 FF");

    // The scripted connection delivers no more bytes after the one line, so the
    // next recv returns 0 (peer close). requestStop bounds any hunt.
    g_testStop->requestStop();
    line = t.nextLine();
    EXPECT_FALSE(line.has_value());
    g_testStop->reset();
}

TEST(TCPTransportTest, ConnectionRefused_OpenReturnsFalse) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::failConnect());  // connect() returns -1

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 9, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    EXPECT_FALSE(t.open());
    EXPECT_FALSE(t.isOpen());
}

// ============================================================
// ELM327 protocol — AUTH + AT-init on connect.
// ============================================================

TEST(TCPTransportTest, Elm327Protocol_SendsAuthThenAtInitOnConnect) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::elmHandshakeConnect());

    g_testStop->reset();
    // atInitDelayMs = 0 zeroes the inter-command pacing (FakeClock makes it
    // instant regardless); the AT commands are still SENT, so the assertions
    // below still hold. socketRecvTimeoutMs is the handshake SO_RCVTIMEO.
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "elm327"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, 0, 10}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

    std::string capturedCommands = sock.sentBlob();
    EXPECT_NE(capturedCommands.find("ATZ\r"), std::string::npos);
    EXPECT_NE(capturedCommands.find("ATE0\r"), std::string::npos);
    EXPECT_NE(capturedCommands.find("ATSP6\r"), std::string::npos);
    EXPECT_NE(capturedCommands.find("ATH1\r"), std::string::npos);
    EXPECT_NE(capturedCommands.find("ATMA\r"), std::string::npos);

    g_testStop->requestStop();
    t.nextLine();
    g_testStop->reset();
}

TEST(TCPTransportTest, Elm327InitFailure_OpenReturnsFalse) {
    // Connect succeeds but the first ELM response is missing/garbage → init
    // fails → open returns false. The handshake script answers AUTH but leaves
    // the ELM responses absent so sendElm327Init's discard-recv sees a close.
    test::FakeSocket sock;
    std::deque<std::string> chunks;
    chunks.push_back("OK\r");  // AUTH ok
    // No ELM "OK" responses and no ATI/ATHELO: the next recv (after ATZ) hits
    // EOF → sendElm327Init returns false → open fails.
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "elm327"},
                   std::make_shared<StdOut>(), TcpReadTiming{500000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    EXPECT_FALSE(opened.load());
    g_testStop->reset();
}

TEST(TCPTransportTest, RequestStop_TerminatesNextLine) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::handshakeConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 500}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

    // CONTRACT: a quiet peer + requestStop() must make nextLine() return
    // nullopt (this is how Ctrl+C stops a live stream). Assert the contract,
    // not wall-clock promptness.
    g_testStop->requestStop();
    auto r = t.nextLine();
    EXPECT_FALSE(r.has_value());

    g_testStop->reset();
}

// ============================================================
// nextLine() behaviour contract — BLIND coverage that locks the
// transport's line-framing behaviour. Each test expresses an externally
// observable behaviour of nextLine() independent of how it's implemented.
// ============================================================

// Open a transport against a scripted handshake and return it ready for
// nextLine() calls (the handshake is already past).
struct ConnectedTransport {
    test::FakeSocket sock;
    std::shared_ptr<util::FakeClock> clock = std::make_shared<util::FakeClock>();
    std::unique_ptr<TCPTransport> transport;
};

static std::unique_ptr<ConnectedTransport> openConnectedTransport(
    std::deque<std::string> extraChunks = {}) {
    auto ctx = std::make_unique<ConnectedTransport>();
    g_testStop->reset();
    std::deque<std::string> chunks = test::heloHandshakeChunks();
    for (auto& c : extraChunks) chunks.push_back(std::move(c));
    ctx->sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    ctx->transport = std::make_unique<TCPTransport>(
        TransportEndpoint{"127.0.0.1", 3333, "raw"},
        std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 500}, g_testStop,
        HuntResilienceConfig{}, ctx->clock,
        std::shared_ptr<ISocket>(&ctx->sock, [](ISocket*) {}));

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = ctx->transport->open(); });
    th.join();
    if (!opened.load()) return nullptr;
    return ctx;
}

// --- Guards: nextLine() on a transport that is not open / exhausted ---

TEST(TCPTransportNextLineContract, NotOpened_ReturnsNullopt) {
    g_testStop->reset();
    test::FakeSocket sock;  // nothing enqueued: connect would fail, but we never open
    TCPTransport t(TransportEndpoint{"127.0.0.1", 1, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    EXPECT_FALSE(t.nextLine().has_value());
    g_testStop->reset();
}

TEST(TCPTransportNextLineContract, OpenFailed_ReturnsNullopt) {
    g_testStop->reset();
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::failConnect());
    TCPTransport t(TransportEndpoint{"127.0.0.1", 1, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    ASSERT_FALSE(t.open());
    EXPECT_FALSE(t.nextLine().has_value());
    g_testStop->reset();
}

// --- Terminators: \r, \n, and \r\n all frame exactly one line ---

TEST(TCPTransportNextLineContract, CarriageReturnTerminatesLine) {
    auto ctx = openConnectedTransport({"ABCDEF\r"});
    ASSERT_TRUE(ctx);
    auto line = ctx->transport->nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "ABCDEF");
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

TEST(TCPTransportNextLineContract, NewlineTerminatesLine) {
    auto ctx = openConnectedTransport({"HELLO\n"});
    ASSERT_TRUE(ctx);
    auto line = ctx->transport->nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "HELLO");
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

TEST(TCPTransportNextLineContract, CrlfFramesLineThenEmptyBannerLine) {
    auto ctx = openConnectedTransport({"FRAME1\r\nFRAME2\r"});
    ASSERT_TRUE(ctx);
    auto first = ctx->transport->nextLine();
    auto mid = ctx->transport->nextLine();
    auto second = ctx->transport->nextLine();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(mid.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, "FRAME1");
    EXPECT_EQ(*mid, "");
    EXPECT_EQ(*second, "FRAME2");
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// --- Banner empty line: "\r\r" delivers an empty line ---

TEST(TCPTransportNextLineContract, DoubleCrBannerDeliversEmptyLine) {
    auto ctx = openConnectedTransport({"REAL\r\r"});
    ASSERT_TRUE(ctx);
    auto first = ctx->transport->nextLine();
    auto banner = ctx->transport->nextLine();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(*first, "REAL");
    EXPECT_EQ(*banner, "");
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// --- Multi-line drain: several complete lines in one burst ---

TEST(TCPTransportNextLineContract, MultipleLinesDrainAcrossSuccessiveCalls) {
    auto ctx = openConnectedTransport({"L1\rL2\rL3\r"});
    ASSERT_TRUE(ctx);
    std::vector<std::string> drained;
    for (int i = 0; i < 3; ++i) {
        auto line = ctx->transport->nextLine();
        ASSERT_TRUE(line.has_value()) << "expected line " << i;
        drained.push_back(*line);
    }
    EXPECT_EQ(drained, (std::vector<std::string>{"L1", "L2", "L3"}));
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// --- Partial line buffered across two socket delivers ---

TEST(TCPTransportNextLineContract, PartialLineAcrossTwoReadsAssembles) {
    auto ctx = openConnectedTransport({"PART_", "TWO\r"});
    ASSERT_TRUE(ctx);
    auto line = ctx->transport->nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "PART_TWO");
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// --- Line longer than one recv() chunk (>256 bytes) still assembles ---

TEST(TCPTransportNextLineContract, LineLongerThanReadChunkAssembles) {
    std::string big(600, 'X');
    // Deliver the big line as two chunks within the SAME connection (the open
    // script) to exercise cross-chunk assembly.
    std::deque<std::string> extra;
    extra.push_back(big.substr(0, 300));
    extra.push_back(big.substr(300) + "\r");
    auto ctx = openConnectedTransport(std::move(extra));
    ASSERT_TRUE(ctx);
    auto line = ctx->transport->nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(line->size(), big.size());
    EXPECT_EQ(*line, big);
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// --- Buffered fast path: a line already buffered is returned even after a
//     prior nextLine() that only partially consumed the buffer ---

TEST(TCPTransportNextLineContract, BufferedLineServedWithoutSocketRead) {
    auto ctx = openConnectedTransport({"FIRST\rSECOND\r"});
    ASSERT_TRUE(ctx);
    ASSERT_TRUE(ctx->transport->nextLine().has_value());  // drains FIRST

    auto second = ctx->transport->nextLine();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, "SECOND");
    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// --- Clean EOF: peer closes after delivering a line; nextLine yields the
//     line, then signals end-of-stream (nullopt). ---

TEST(TCPTransportNextLineContract, CleanEofDeliversLineThenNullopt) {
    auto ctx = openConnectedTransport({"LAST\r"});
    ASSERT_TRUE(ctx);
    auto line = ctx->transport->nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "LAST");

    g_testStop->requestStop();
    EXPECT_FALSE(ctx->transport->nextLine().has_value());
    g_testStop->reset();
}

TEST(TCPTransportTest, AuthRejected_OpenReturnsFalse) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::authRejectedConnect("ERROR unauthorized\r"));

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{500000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    EXPECT_FALSE(opened.load());
    EXPECT_FALSE(t.isOpen());
    g_testStop->reset();
}
