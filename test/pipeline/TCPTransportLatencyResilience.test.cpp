// Latency + resilience seams for TCPTransport (Phase 1 + Phase 3):
//   - setNoDelay(true) is requested on the stream socket after connect (Nagle off).
//   - open() is ordering-independent: if the peer is not up at start, it retries
//     with a bounded backoff (connectUntilUp) instead of failing hard.
//   - performPing() round-trips a PING<PONG keepalive frame, returning RTT.
//
// All network I/O is scripted through FakeSocket (no real socket / device).

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <deque>
#include <memory>
#include <string>
#include <thread>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::util;
namespace util = vehicle_sim::util;

static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

// PONG reply to a PING <seq> — models the firmware TcpServerManager echo.
inline std::deque<std::string> pingHandshakeChunks(int seq) {
    std::deque<std::string> c = test::heloHandshakeChunks();
    c.push_back("PONG " + std::to_string(seq) + "\r");
    return c;
}

// ----------------------------------------------------------------
// SEAM 1: Nagle disabled on the stream socket.
// ----------------------------------------------------------------
TEST(TCPTransportLatencyTest, RequestsNoDelayOnConnectedStream) {
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

    // The transport must have asked the socket to disable Nagle on the CAN stream.
    EXPECT_TRUE(sock.setNoDelayRequested())
        << "TCP_NODELAY was not requested on the stream socket — Nagle stays on "
           "and small frames can stall up to the delayed-ACK window.";

    g_testStop->requestStop();
    EXPECT_FALSE(t.nextLine().has_value());
    g_testStop->reset();
}

// ----------------------------------------------------------------
// SEAM 2: ordering-independent first connect (retry-until-up).
// ----------------------------------------------------------------
TEST(TCPTransportResilienceTest, OpenRetriesUntilPeerComesUp) {
    test::FakeSocket sock;
    // First connect attempt: host unreachable (refused). Second: handshake ok.
    // This models "ESP32/WiFi not up at client start" — open() must NOT fail.
    sock.enqueue("10.0.0.42", test::failConnect());
    sock.enqueue("10.0.0.42", test::handshakeConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"10.0.0.42", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();

    // The bounded retry loop converts the initial refusal into a successful open.
    EXPECT_TRUE(opened.load())
        << "open() must retry with backoff when the peer is not yet up, not fail hard.";
    // The transport must have reached AUTH on the successful connect.
    EXPECT_NE(sock.sentBlob().find("AUTH"), std::string::npos)
        << "open() must complete the handshake once the peer comes up";

    g_testStop->requestStop();
    g_testStop->reset();
}

TEST(TCPTransportResilienceTest, OpenGivesUpAfterBudgetExhausted) {
    test::FakeSocket sock;
    // Every attempt refused — models a peer that never comes up. Under FakeClock
    // the backoff advances virtual time instantly, so the 30s budget is consumed
    // deterministically and open() returns false (no infinite loop, no crash).
    for (int i = 0; i < 12; ++i) sock.enqueue("10.0.0.99", test::failConnect());

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"10.0.0.99", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();

    // A perpetually unreachable peer eventually yields false (budget exhausted)
    // instead of failing hard or hanging forever — under FakeClock the 30s budget
    // is virtual-time and consumed deterministically in ~1ms.
    EXPECT_FALSE(opened.load())
        << "open() must give up after the first-connect budget is exhausted, "
           "not loop forever or crash";

    g_testStop->requestStop();
    g_testStop->reset();
}

// ----------------------------------------------------------------
// SEAM 3: ping/PONG keepalive returns RTT, reuses the stream seam.
// ----------------------------------------------------------------
TEST(TCPTransportKeepaliveTest, PerformPingRoundTripsAndReturnsRtt) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, pingHandshakeChunks(7)});

    g_testStop->reset();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

    const long rtt = t.performPing(7, 2000);
    // FakeClock makes the round trip instantaneous, but the contract is: a
    // matching PONG returns a non-negative RTT rather than -1 (timeout/fail).
    EXPECT_GE(rtt, 0)
        << "performPing must return RTT on a matching PONG, not -1";

    g_testStop->requestStop();
    EXPECT_FALSE(t.nextLine().has_value());
    g_testStop->reset();
}

TEST(TCPTransportKeepaliveTest, PerformPingReturnsMinusOneOnTimeout) {
    test::FakeSocket sock;
    // Handshake only — no PONG queued, so the keepalive must time out.
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

    // With no PONG scripted, the keepalive must report -1 (link stall / timeout),
    // which is exactly the signal that should drive the hunt.
    EXPECT_EQ(t.performPing(3, 50), -1)
        << "performPing must report -1 when no matching PONG arrives";

    g_testStop->requestStop();
    g_testStop->reset();
}
