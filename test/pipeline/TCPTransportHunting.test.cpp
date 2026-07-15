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
class SameIpDiscoveryListener : public IDiscoveryListener {
public:
    explicit SameIpDiscoveryListener(std::string address) : address_(std::move(address)) {}
    bool start() override { return true; }
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds /*timeout*/) override {
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
};

DiscoveryListenerFactory sameIpDiscoveryFactory(std::string address) {
    return [addr = std::move(address)]() {
        return std::unique_ptr<IDiscoveryListener>(std::make_unique<SameIpDiscoveryListener>(addr));
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
TEST(TCPTransportHuntingTest, DiscoveryFindsNewIp_SwitchesHostAndReturnsTrue) {
    test::FakeSocket sock;
    // Old IP (unreachable) connect fails; the discovered new IP (127.0.0.1)
    // connect succeeds.
    sock.enqueue(kUnreachableIp, test::failConnect());
    sock.enqueue(kLoopbackIp, test::handshakeConnect());

    auto stop = makeStop();
    // Discovery deterministically reports a device at 127.0.0.1 (hermetic —
    // no real UDP broadcast).
    auto transport = makeTransport(sock, kUnreachableIp, 3333, stop,
                                   HuntResilienceConfig{sameIpDiscoveryFactory(kLoopbackIp), {}});

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

#endif // VEHICLE_SIM_HUNTING_ENABLED
