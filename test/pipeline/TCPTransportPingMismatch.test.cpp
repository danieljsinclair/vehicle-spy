// TCPTransportPingMismatch.test.cpp — TDD spec for the performPing mismatch
// paths (gap c) and the IClock routing pre-step.
//
// Gaps pinned:
//   (c1) wrong-PONG / seq-mismatch → -1 (not a false-positive RTT).
//   (c2) performPing routes through IClock::now() so a FakeClock can drive the
//        deadline without real wall-clock wait (the pre-step the plan requires
//        before the TcpReader extraction can safely relocate performPing).
//
// CHARACTERISATION tests: lock the keepalive contract using FakeSocket
// (scriptable PONG / EOF) and FakeClock (deterministic deadline).

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <deque>
#include <memory>
#include <thread>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

// PONG reply to a PING <seq> — models the firmware TcpServerManager echo.
inline std::deque<std::string> pingHandshakeChunks(int seq) {
    std::deque<std::string> c = test::heloHandshakeChunks();
    c.push_back("PONG " + std::to_string(seq) + "\r");
    return c;
}

// Open a transport against a scripted handshake and return it ready for
// performPing() calls.
struct ReadyForPing {
    test::FakeSocket sock;
    std::shared_ptr<util::FakeClock> clock = std::make_shared<util::FakeClock>();
    std::unique_ptr<TCPTransport> transport;
};

static std::unique_ptr<ReadyForPing> openReadyForPing(
    std::deque<std::string> extraChunks = {}) {
    auto ctx = std::make_unique<ReadyForPing>();
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

// ── IClock routing: performPing must route time reads through IClock ─────────
//
// performPing must call clock_->now() to check the deadline, not
// std::chrono::steady_clock::now(). A RecordingClock captures the now()
// invocations; the assertion is: if routing is wired, now() is called at
// least once during a timeout-returning performPing.

class PingClock : public util::IClock {
public:
    using time_point = util::IClock::time_point;
    using duration = util::IClock::duration;

    PingClock() : now_{time_point::min()} {}

    [[nodiscard]] time_point now() const override {
        nowCalls_++;
        // Advance 1 ms per call so the deadline (t0 + timeoutMs) is reached
        // after ~timeoutMs loop iterations. Starting at epoch (not min()) keeps
        // the deadline in the future until enough now() calls accumulate,
        // forcing the loop to iterate and proving the routing is active.
        now_ += std::chrono::milliseconds(1);
        return now_;
    }

    void sleepFor(std::chrono::milliseconds) override {
        // performPing never sleeps; no-op keeps the loop tight and fast.
    }

    int nowCallCount() const { return nowCalls_; }

protected:
    [[nodiscard]] bool waitForImpl(
        std::condition_variable&, std::unique_lock<std::mutex>&,
        const std::function<bool()>&, time_point) const override {
        return false; // not used
    }

private:
    mutable int nowCalls_{0};
    mutable time_point now_;
};

TEST(TCPTransportPingMismatchTest, PerformPing_RoutesDeadlineThroughIClock) {
    test::FakeSocket sock;
    sock.enqueue("127.0.0.1", test::handshakeConnect());

    g_testStop->reset();
    auto clock = std::make_shared<PingClock>();
    TCPTransport t(TransportEndpoint{"127.0.0.1", 3333, "raw"},
                   std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
                   HuntResilienceConfig{}, clock,
                   std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    ASSERT_TRUE(opened.load());

    // No PONG queued. PingClock advances 1 ms per now() call, so the 100 ms
    // timeout expires after ~100 loop iterations and performPing returns -1.
    const long rtt = t.performPing(1, 100);
    EXPECT_EQ(rtt, -1)
        << "performPing must return -1 when no PONG arrives within the timeout.";
    EXPECT_GT(clock->nowCallCount(), 0)
        << "performPing must call clock_->now() to drive the deadline "
           "(proves the IClock routing is wired, not std::chrono::steady_clock).";

    g_testStop->requestStop();
    t.nextLine();
    g_testStop->reset();
}

// ── Wrong PONG (seq mismatch): must return -1, never a false-positive RTT ────

TEST(TCPTransportPingMismatchTest, WrongPong_ReturnsMinusOne) {
    auto ctx = openReadyForPing({"PONG 999\r"});
    ASSERT_TRUE(ctx);
    g_testStop->reset();

    // PING 7 but peer answers PONG 999 (wrong seq). Must return -1.
    const long rtt = ctx->transport->performPing(7, 2000);
    EXPECT_EQ(rtt, -1)
        << "performPing must return -1 when the PONG seq does not match the "
           "PING seq (must not return a false-positive RTT).";

    g_testStop->requestStop();
    g_testStop->reset();
}

// ── Mangled PONG (partial prefix match): must not confuse with valid PONG ────

TEST(TCPTransportPingMismatchTest, MangledPong_PrefixMismatch_ReturnsMinusOne) {
    auto ctx = openReadyForPing({"PON 7\r"});  // "PON 7" — prefix of "PONG 7" but not equal
    ASSERT_TRUE(ctx);
    g_testStop->reset();

    const long rtt = ctx->transport->performPing(7, 500);
    EXPECT_EQ(rtt, -1)
        << "performPing must not accept a partial prefix as a matching PONG.";

    g_testStop->requestStop();
    g_testStop->reset();
}

// ── Stray frame before PONG: frame is skipped, PONG still found ──────────────
//
// The keepalive reuses the same recv seam as nextLine(), so a stray CAN frame
// interleaved with the PONG must not desync the matcher.

TEST(TCPTransportPingMismatchTest, StrayFrameBeforePong_FrameSkippedPongFound) {
    auto ctx = openReadyForPing({"7E8 03 41 00 FF FF FF\r", "PONG 7\r"});
    ASSERT_TRUE(ctx);
    g_testStop->reset();

    const long rtt = ctx->transport->performPing(7, 2000);
    EXPECT_GE(rtt, 0)
        << "performPing must skip an interleaved CAN frame and still match the PONG.";

    g_testStop->requestStop();
    g_testStop->reset();
}
