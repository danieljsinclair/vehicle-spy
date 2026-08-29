// TCPTransportHunting.test.cpp
//
// BLIND SPEC-FIRST TDD for TCPTransport::enterHuntingState() (src/pipeline/TCPTransport.cpp).
//
// The hunt drives the REAL production function (no mocks of the transport or
// UDPDiscovery path). Network I/O is scripted through a FakeSocket (no real
// socket / loopback server), time is instant via a FakeClock (the backoff
// sleeps route through IClock::sleepFor), and discovery is hermetic via the
// injected IDiscoveryListener factory (real UDP is never bound). This keeps the
// original assertions intact while removing the ~39 s of real I/O.

#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/discovery/DiscoveryPacket.h"
#include "vehicle-sim/discovery/DiscoveredDevice.h"
#include "vehicle-sim/discovery/IDiscoveryListener.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;
using namespace vehicle_sim::discovery;

#if defined(VEHICLE_SIM_HUNTING_ENABLED)

// Expose the private enterHuntingState() and host_ for direct invocation.
#define private public
#include "vehicle-sim/pipeline/TCPTransport.h"
#undef private

namespace {

constexpr const char* kLoopbackIp = "127.0.0.1";
constexpr const char* kUnreachableIp = "127.0.0.2";

// Deterministic discovery listener that always reports ONE device at `address`.
//
// Deterministic thread handshake (zero sleeps/timeouts, both directions):
//   - `discoveryLiveProm` (optional): the FIRST poll() fires it the instant
//     the discovery thread is PAST runDiscovery's loop guard (i.e. it is
//     inside poll(), so the guard can no longer short-circuit on
//     shouldStopDiscovery). Wire hunt.onHuntStarted to WAIT on its future and
//     the main hunt loop cannot make ANY progress until the discovery thread
//     is live. This closes the flaky race where FakeClock's instant backoff
//     let the main loop exhaust ALL retries in microseconds while the
//     discovery std::thread had not been scheduled yet — runDiscovery's guard
//     then saw shouldStopDiscovery already set, exited without a single poll,
//     and finalizeHunt reported "neither old IP nor discovery succeeded".
//   - `reportGateProm` (optional): the first poll() then BLOCKS until this
//     promise is fired, so the device is reported only after the test-chosen
//     event (e.g. the first "rapid retry" marker). This pins the phase
//     ordering a test asserts (rapid phase fires BEFORE discovery wins)
//     without weakening its assertions.
// Both promises are one-shot: subsequent polls return immediately.
class SameIpDiscoveryListener : public IDiscoveryListener {
public:
    SameIpDiscoveryListener(std::string address,
                            std::shared_ptr<std::promise<void>> discoveryLiveProm = nullptr,
                            std::shared_ptr<std::promise<void>> reportGateProm = nullptr)
        : address_(std::move(address))
        , discoveryLiveProm_(std::move(discoveryLiveProm))
        , reportGateProm_(std::move(reportGateProm)) {}
    bool start() override { return true; }
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds /*timeout*/) override {
        // Signal "discovery is live" BEFORE anything else: the hunt's
        // onHuntStarted (main thread) waits on this, so the main loop only
        // proceeds once this thread is past the runDiscovery loop guard.
        if (discoveryLiveProm_) {
            discoveryLiveProm_->set_value();
            discoveryLiveProm_.reset();
        }
        // Optionally hold the FIRST report until the test's gate event, so
        // discovery cannot win before the phase the test pins has fired.
        if (reportGateProm_) {
            reportGateProm_->get_future().wait();
            reportGateProm_.reset();
        }
        DiscoveredDevice d{};
        for (size_t i = 0; i < d.deviceId.size(); ++i) d.deviceId[i] = static_cast<uint8_t>(i);
        d.address = address_;
        d.canPort = 3333;
        d.otaPort = 80;
        d.timestamp = 12345;
        return {d};
    }
    void stop() override {}
private:
    std::string address_;
    std::shared_ptr<std::promise<void>> discoveryLiveProm_;
    std::shared_ptr<std::promise<void>> reportGateProm_;
};

// `discoveryLiveProm` / `reportGateProm` (optional) are captured into the
// listener per the class comment — wiring the discovery-live rendezvous (and
// optional report gate) so the test is deterministic (zero sleep/polling).
DiscoveryListenerFactory sameIpDiscoveryFactory(
    std::string address,
    std::shared_ptr<std::promise<void>> discoveryLiveProm = nullptr,
    std::shared_ptr<std::promise<void>> reportGateProm = nullptr) {
    return [addr = std::move(address), discoveryLiveProm, reportGateProm]() {
        return std::unique_ptr<IDiscoveryListener>(
            std::make_unique<SameIpDiscoveryListener>(addr, discoveryLiveProm, reportGateProm));
    };
}

// No-op discovery listener: never reports a device.
class NoOpDiscoveryListener : public IDiscoveryListener {
public:
    bool start() override { return true; }
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds /*timeout*/) override {
        return {};
    }
    void stop() override {}
};

DiscoveryListenerFactory noOpDiscoveryFactory() {
    return []() { return std::make_unique<NoOpDiscoveryListener>(); };
}

// Silent output sink (discard everything).
class QuietOutput : public ITransportOutput {
public:
    void out(const std::string& /*msg*/) override {}
    void err(const std::string& /*msg*/) override {}
};

// Build a transport wired to a scripted FakeSocket (non-owning) + instant
// FakeClock + the given discovery factory.
std::unique_ptr<TCPTransport> makeTransport(
    test::FakeSocket& sock, const std::string& host, int port,
    std::shared_ptr<StopToken> stop, HuntResilienceConfig hunt = HuntResilienceConfig{}) {
    auto clock = std::make_shared<util::FakeClock>();
    return std::make_unique<TCPTransport>(
        TransportEndpoint{host, port, "raw"},
        std::make_shared<QuietOutput>(), TcpReadTiming{}, std::move(stop),
        std::move(hunt), std::move(clock),
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

std::shared_ptr<StopToken> makeStop() { return std::make_shared<StopToken>(); }

} // namespace

// Spec 1: OLD-IP RECONNECTION WINS
TEST(TCPTransportHuntingTest, OldIpReachable_ReconnectsAndReturnsTrue) {
    test::FakeSocket sock;
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // old-IP reconnect succeeds on attempt 1

    auto stop = makeStop();
    // noOpDiscoveryFactory: discovery is irrelevant (old-IP wins) and the
    // post-loop join doesn't block on a real UDP poll. FakeClock makes the
    // ~1000 ms backoff instant.
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop,
                                   HuntResilienceConfig{noOpDiscoveryFactory(), {}});

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result) << "old-IP reconnection should succeed";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "host_ must NOT switch when old IP wins";
    EXPECT_EQ(sock.connectCount(), 1)
        << "exactly one reconnection handshake expected";
    EXPECT_FALSE(stop->stopRequested()) << "should not have been stopped";
}

// Spec 2: DISCOVERY FINDS A NEW IP -> SWITCH
//
// Determinism: the discovery listener's FIRST poll() fires discoveryLive the
// instant it is past runDiscovery's loop guard, and onHuntStarted (main
// thread, fired once before any retry/backoff attempt) WAITS on that signal —
// so the discovery thread's readiness is a guaranteed precondition of the
// main loop's progress. Without this, FakeClock's instant backoff lets the
// main loop exhaust MAX_RETRIES while the discovery std::thread has not been
// scheduled yet: runDiscovery's guard then sees shouldStopDiscovery and exits
// without a single poll, so discoveryFound never flips and finalizeHunt
// reports "neither old IP nor discovery succeeded" (the flaky failure this
// test exhibited under full-suite load). A win landing ANY time before the
// post-loop join is still honored — finalizeHunt reads discoveryFound after
// the join — so with the rendezvous every interleaving passes.
TEST(TCPTransportHuntingTest, DiscoveryFindsNewIp_SwitchesHostAndReturnsTrue) {
    test::FakeSocket sock;
    // Old IP (unreachable) connect fails; the discovered new IP (127.0.0.1)
    // connect succeeds.
    sock.enqueue(kUnreachableIp, test::failConnect());
    sock.enqueue(kLoopbackIp, test::handshakeConnect());

    auto stop = makeStop();
    // Rendezvous: discovery fires discoveryLive from inside its first poll();
    // the hunt's onHuntStarted hook blocks on it before the retry loop runs.
    auto discoveryLiveProm = std::make_shared<std::promise<void>>();
    std::shared_future<void> discoveryLiveFut = discoveryLiveProm->get_future().share();
    HuntResilienceConfig hunt{sameIpDiscoveryFactory(kLoopbackIp, discoveryLiveProm), {}};
    hunt.onHuntStarted = [discoveryLiveFut]() { discoveryLiveFut.wait(); };
    auto transport = makeTransport(sock, kUnreachableIp, 3333, stop, std::move(hunt));

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result) << "discovery-driven switch + reconnect should succeed";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "host_ must switch to the discovered IP (127.0.0.1)";
    EXPECT_EQ(sock.connectCount(), 1)
        << "one reconnection to the new IP expected";
}

// Spec 3: NEITHER PATH SUCCEEDS -> GIVES UP
TEST(TCPTransportHuntingTest, NoReconnectAndNoDiscovery_ReturnsFalse) {
    test::FakeSocket sock;
    sock.enqueue(kUnreachableIp, test::failConnect());  // old IP unreachable, connect always fails

    auto stop = makeStop();

    // Signal the instant the hunt loop goes live (onHuntStarted fires once at
    // the loop top) — await it with zero polling/sleep.
    auto huntStartedProm = std::make_shared<std::promise<void>>();
    std::future<void> huntStartedFut = huntStartedProm->get_future();
    HuntResilienceConfig hunt{noOpDiscoveryFactory(), {}};
    hunt.onHuntStarted = [huntStartedProm]() { huntStartedProm->set_value(); };
    auto transport = makeTransport(sock, kUnreachableIp, 9, stop, std::move(hunt));

    std::future<bool> fut = std::async(std::launch::async, [&]() {
        return transport->enterHuntingState();
    });

    huntStartedFut.wait();  // returns the instant the hunt is inside its loop
    transport->requestStop();

    bool result = fut.get();

    EXPECT_FALSE(result) << "hunt must give up when neither old IP nor discovery succeeds";
    EXPECT_EQ(transport->host_, std::string(kUnreachableIp))
        << "host_ must NOT switch when nothing succeeded";
}

// Spec 4: DISCOVERY AT SAME IP AS OLD HOST IS IGNORED
TEST(TCPTransportHuntingTest, DiscoverySameIpAsOldHost_DoesNotSwitch) {
    test::FakeSocket sock;
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // old-IP (127.0.0.1) wins on attempt 1

    auto stop = makeStop();
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop,
                                   HuntResilienceConfig{sameIpDiscoveryFactory(kLoopbackIp), {}});

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result) << "old-IP reconnection should still succeed";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "discovery at the same IP must NOT switch host_";
    EXPECT_EQ(sock.connectCount(), 1);
}

// Capturing output sink: stores every out()/err() string for later assertion.
// When `rapidFiredProm` is set, the FIRST message containing "rapid retry"
// fires it — the deterministic gate the discovery listener can wait on so a
// device is only reported after the rapid phase has visibly fired.
class CapturingOutput : public ITransportOutput {
public:
    CapturingOutput() = default;
    explicit CapturingOutput(std::shared_ptr<std::promise<void>> rapidFiredProm)
        : rapidFiredProm_(std::move(rapidFiredProm)) {}
    void out(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(msg);
        maybeFireRapidGate(msg);
    }
    void err(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(msg);
        maybeFireRapidGate(msg);
    }
    std::string blob() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::string s;
        for (const auto& l : lines_) { s += l; s += "\n"; }
        return s;
    }
private:
    // Called with mu_ held (set_value is safe under the lock: the waiter's
    // poll() only waits, it never re-enters this sink).
    void maybeFireRapidGate(const std::string& msg) {
        if (rapidFiredProm_ && msg.find("rapid retry") != std::string::npos) {
            rapidFiredProm_->set_value();
            rapidFiredProm_.reset();
        }
    }
    mutable std::mutex mu_;
    std::vector<std::string> lines_;
    std::shared_ptr<std::promise<void>> rapidFiredProm_;
};

// Build a transport wired to a scripted FakeSocket + instant FakeClock + the
// given discovery factory, using a capturing output sink (for message asserts).
std::unique_ptr<TCPTransport> makeTransportCapturing(
    test::FakeSocket& sock, const std::string& host, int port,
    std::shared_ptr<StopToken> stop, HuntResilienceConfig hunt,
    std::shared_ptr<CapturingOutput> cap) {
    auto clock = std::make_shared<util::FakeClock>();
    return std::make_unique<TCPTransport>(
        TransportEndpoint{host, port, "raw"},
        cap, TcpReadTiming{}, std::move(stop),
        std::move(hunt), std::move(clock),
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

// Spec 5: AGGRESSIVE IMMEDIATE RETRY — last-known IP:port is retried with
// MINIMAL backoff (first retry has zero delay), succeeding within RAPID_RETRIES
// before the exponential backoff loop ever starts. With FakeClock the whole
// rapid phase is instant, proving the reconnect path does NOT wait
// BASE_RETRY_DELAY_MS (1000ms) on the first reconnect.
TEST(TCPTransportHuntingTest, RapidReconnect_ImmediateRetryReconnectsLastKnownIp) {
    test::FakeSocket sock;
    // First rapid attempt succeeds (last-known IP is reachable again).
    sock.enqueue(kLoopbackIp, test::handshakeConnect());

    auto stop = makeStop();
    // No-op discovery: rapid phase wins purely on last-known-IP retry.
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop,
                                   HuntResilienceConfig{noOpDiscoveryFactory(), {}});

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result) << "rapid reconnect to last-known IP should succeed";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "host_ must NOT switch on a rapid last-known-IP win";
    EXPECT_EQ(sock.connectCount(), 1)
        << "exactly one connect expected (immediate rapid retry, no backoff)";
}

// Spec 6: RAPID RETRY MESSAGE — the hunt emits a distinct "rapid retry" message
// so the operator can see the aggressive immediate-retry phase firing (vs the
// slower exponential backoff that follows it). Asserts on the intent/marker of
// the message, not exact wording.
TEST(TCPTransportHuntingTest, RapidReconnect_EmitsRapidRetryMessage) {
    test::FakeSocket sock;
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // rapid attempt 1 succeeds

    auto stop = makeStop();
    auto cap = std::make_shared<CapturingOutput>();
    auto transport = makeTransportCapturing(sock, kLoopbackIp, 3333, stop,
                                             HuntResilienceConfig{noOpDiscoveryFactory(), {}},
                                             cap);

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result);
    std::string msgs = cap->blob();
    EXPECT_NE(msgs.find("rapid retry"), std::string::npos)
        << "aggressive immediate-retry phase must emit a 'rapid retry' marker; "
        << "blob:\n" << msgs;
    // Rapid-phase success reuses the existing last-known-IP reconnect marker.
    EXPECT_NE(msgs.find("reconnected to old IP"), std::string::npos)
        << "rapid-phase success must emit a reconnect marker; blob:\n" << msgs;
}

// Spec 7: RAPID PHASE EXHAUSTS INTO DISCOVERY SWITCH — when the last-known IP is
// unreachable for all RAPID_RETRIES attempts, the hunt must NOT give up but fall
// through to the discovery-driven switch and eventually succeed (order-
// independent, self-recovering). Discovery supplies the new IP after the rapid
// phase fails, proving the two phases are chained, not exclusive.
//
// Determinism (zero sleeps/timeouts): TWO one-directional handshakes pin both
// orderings this test asserts:
//   - discoveryLive (listener's first poll() -> onHuntStarted waits): the main
//     hunt loop cannot progress until the discovery thread is past
//     runDiscovery's loop guard — so it can never exhaust all (instant)
//     retries and stop the discovery thread before its first poll runs.
//   - rapidFired (CapturingOutput on the first "rapid retry" marker ->
//     listener's first poll() waits): the device is only reported after the
//     rapid phase has visibly fired, so discovery can never win before the
//     ">= 1 rapid marker" assertion's event. Any interleaving afterwards
//     passes: discoveryFound flips at worst by the post-loop join, which
//     happens-before finalizeHunt reads it.
TEST(TCPTransportHuntingTest, RapidReconnect_ExhaustsThenDiscoverySwitches) {
    test::FakeSocket sock;
    // All RAPID_RETRIES attempts fail on the old (unreachable) IP (FakeSocket: a
    // failConnect does NOT increment connectCount — only successful connects do).
    for (int i = 0; i < TCPTransport::RAPID_RETRIES; ++i) {
        sock.enqueue(kUnreachableIp, test::failConnect());
    }
    // ...then discovery reports a new reachable IP, and the post-rapid connect wins.
    sock.enqueue(kLoopbackIp, test::handshakeConnect());

    auto stop = makeStop();
    auto discoveryLiveProm = std::make_shared<std::promise<void>>();
    auto rapidFiredProm = std::make_shared<std::promise<void>>();
    std::shared_future<void> discoveryLiveFut = discoveryLiveProm->get_future().share();
    auto cap = std::make_shared<CapturingOutput>(rapidFiredProm);
    HuntResilienceConfig hunt{sameIpDiscoveryFactory(kLoopbackIp, discoveryLiveProm, rapidFiredProm), {}};
    hunt.onHuntStarted = [discoveryLiveFut]() { discoveryLiveFut.wait(); };
    auto transport = makeTransportCapturing(sock, kUnreachableIp, 3333, stop,
                                             std::move(hunt),
                                             cap);

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result) << "hunt must recover after rapid phase via discovery switch";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "host_ must switch to the discovered IP after rapid phase exhausts";
    std::string msgs = cap->blob();
    // The rapid phase must have fired FIRST (at least one "rapid retry" marker)
    // before discovery won — proving the aggressive immediate-retry runs before
    // the discovery-driven switch. The rapid phase is discovery-interruptible by
    // design (it aborts the instant discovery finds a new IP), so we assert >=1
    // rather than exactly RAPID_RETRIES here.
    int rapidMarkers = 0;
    size_t pos = 0;
    while ((pos = msgs.find("rapid retry", pos)) != std::string::npos) { ++rapidMarkers; pos += 1; }
    EXPECT_GE(rapidMarkers, 1)
        << "rapid phase must fire before discovery wins; blob:\n" << msgs;
    // ...and discovery then drove the switch.
    EXPECT_NE(msgs.find("switching to discovered IP"), std::string::npos)
        << "discovery must drive the switch after the rapid phase fires";
}

#endif // VEHICLE_SIM_HUNTING_ENABLED
