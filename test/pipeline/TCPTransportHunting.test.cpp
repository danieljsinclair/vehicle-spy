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
// When `huntStartedProm` is set, poll() blocks the FIRST time until the
// hunt-loop-live signal is received — i.e. until TCPTransport has fired its
// onHuntStarted hook (TCPTransport.cpp:623, once at the retry-loop top, before
// any reconnect attempt). This eliminates the flaky race where the discovery
// thread could report its device before the main thread's retry loop is
// polling, letting MAX_RETRIES exhaust before discovery wins. The promise is
// one-shot: subsequent polls return immediately.
class SameIpDiscoveryListener : public IDiscoveryListener {
public:
    SameIpDiscoveryListener(std::string address,
                            std::shared_ptr<std::promise<void>> huntStartedProm)
        : address_(std::move(address)), huntStartedProm_(std::move(huntStartedProm)) {}
    bool start() override { return true; }
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds /*timeout*/) override {
        // Wait for the hunt loop to be live before reporting the FIRST device,
        // so the main thread's retry loop is guaranteed to be polling when we
        // set discoveryFound. After the first report this is a no-op.
        if (huntStartedProm_) {
            huntStartedProm_->get_future().wait();
            huntStartedProm_.reset();
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
    std::shared_ptr<std::promise<void>> huntStartedProm_;
};

// `huntStartedProm` (optional) is captured into the listener so its first
// poll() blocks until onHuntStarted fires — wiring the hunt-started signal
// into discovery so the test is deterministic (zero sleep/polling).
DiscoveryListenerFactory sameIpDiscoveryFactory(
    std::string address,
    std::shared_ptr<std::promise<void>> huntStartedProm = nullptr) {
    return [addr = std::move(address), huntStartedProm]() {
        return std::unique_ptr<IDiscoveryListener>(
            std::make_unique<SameIpDiscoveryListener>(addr, huntStartedProm));
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
// Determinism: the discovery listener blocks its FIRST poll() until the hunt
// loop is live (onHuntStarted fires once at the retry-loop top). Without this,
// the discovery thread can race ahead of the main retry loop and report before
// the loop is polling — so under load MAX_RETRIES may exhaust before discovery
// wins (the flaky failure this test exhibited in the full suite). Wiring the
// signal makes discovery report only after the loop is guaranteed to observe
// discoveryFound, eliminating the race.
TEST(TCPTransportHuntingTest, DiscoveryFindsNewIp_SwitchesHostAndReturnsTrue) {
    test::FakeSocket sock;
    // Old IP (unreachable) connect fails; the discovered new IP (127.0.0.1)
    // connect succeeds.
    sock.enqueue(kUnreachableIp, test::failConnect());
    sock.enqueue(kLoopbackIp, test::handshakeConnect());

    auto stop = makeStop();
    // Signal the instant the hunt loop goes live (onHuntStarted fires once at
    // the loop top) — the discovery listener awaits it before reporting.
    auto huntStartedProm = std::make_shared<std::promise<void>>();
    HuntResilienceConfig hunt{sameIpDiscoveryFactory(kLoopbackIp, huntStartedProm), {}};
    hunt.onHuntStarted = [huntStartedProm]() { huntStartedProm->set_value(); };
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
class CapturingOutput : public ITransportOutput {
public:
    void out(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(msg);
    }
    void err(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(msg);
    }
    std::string blob() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::string s;
        for (const auto& l : lines_) { s += l; s += "\n"; }
        return s;
    }
private:
    mutable std::mutex mu_;
    std::vector<std::string> lines_;
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
    auto cap = std::make_shared<CapturingOutput>();
    auto transport = makeTransportCapturing(sock, kUnreachableIp, 3333, stop,
                                             HuntResilienceConfig{sameIpDiscoveryFactory(kLoopbackIp), {}},
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
