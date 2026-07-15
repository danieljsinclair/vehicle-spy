// TCPTransportHuntingCancel.test.cpp
//
// BLIND SPEC-FIRST TDD for the CANCELLABLE-CONNECT contract of
// TCPTransport::enterHuntingState() (src/pipeline/TCPTransport.cpp).
//
// Network I/O is scripted through FakeSocket (no real socket / loopback),
// time is instant via FakeClock (backoff routes through IClock::sleepFor), and
// discovery is hermetic via the injected no-op IDiscoveryListener factory (no
// real UDP). The original cancellability assertions are preserved.

#include "vehicle-sim/discovery/IDiscoveryListener.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

#define private public
#include "vehicle-sim/pipeline/TCPTransport.h"
#undef private

#if defined(VEHICLE_SIM_HUNTING_ENABLED)

namespace {

constexpr const char* kUnreachableIp = "127.0.0.2";
constexpr int kDiscardPort = 9;

std::shared_ptr<StopToken> makeStop() { return std::make_shared<StopToken>(); }

// SilentOutput discards all transport log output.
std::shared_ptr<ITransportOutput> quietOutput() {
    return std::make_shared<SilentOutput>();
}

// No-op IDiscoveryListener: the hermetic seam. start() always succeeds;
// poll() returns no devices. Injected via the TCPTransport ctor's
// DiscoveryListenerFactory so the hunt's discovery-win path is unreachable.
class NoOpDiscoveryListener : public vehicle_sim::discovery::IDiscoveryListener {
public:
    bool start() override { return true; }
    std::vector<vehicle_sim::discovery::DiscoveredDevice> poll(
        std::chrono::milliseconds /*timeout*/) override {
        return {};
    }
    void stop() override {}
};

DiscoveryListenerFactory noOpDiscoveryFactory() {
    return []() { return std::make_unique<NoOpDiscoveryListener>(); };
}

// Build a transport wired to a scripted FakeSocket + instant FakeClock + the
// no-op discovery factory. The socket always fails connect (unreachable host).
// `huntStartedProm` is shared with the test so it can await hunt-start on the
// onHuntStarted signal (zero sleep). May be null when the test doesn't need it.
std::unique_ptr<TCPTransport> makeTransport(
    test::FakeSocket& sock, const std::string& host, int port,
    std::shared_ptr<StopToken> stop,
    std::shared_ptr<std::promise<void>> huntStartedProm = nullptr) {
    auto clock = std::make_shared<util::FakeClock>();
    HuntResilienceConfig hunt{noOpDiscoveryFactory(), {}};
    if (huntStartedProm) {
        hunt.onHuntStarted = [huntStartedProm]() { huntStartedProm->set_value(); };
    }
    return std::make_unique<TCPTransport>(
        TransportEndpoint{host, port, "raw"},
        quietOutput(), TcpReadTiming{}, std::move(stop),
        std::move(hunt), std::move(clock),
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

constexpr int kPromptStopMs = 2000;
constexpr int kExhaustionImpossibleBelowMs = TCPTransport::BASE_RETRY_DELAY_MS;

// Await the hunt going live on the onHuntStarted signal (zero polling/sleep):
// the production enterHuntingState() fires the hook once at its retry-loop top,
// and the future resolves the instant that happens.
void awaitHuntStarted(std::future<void>& huntStartedFut) {
    huntStartedFut.wait();
}

long elapsedMs(std::chrono::steady_clock::time_point from,
               std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

} // namespace

// Spec 1: requestStop() interrupts the hunt PROMPTLY — AND (via the
// elimination argument) is the LOAD-BEARING proof that requestStop is what
// interrupts, not self-exhaustion.
TEST(TCPTransportHuntingCancelTest, RequestStopInterruptsAndIsLoadBearing) {
    test::FakeSocket sock;
    sock.enqueue(kUnreachableIp, test::failConnect());  // old IP unreachable, connect always fails

    auto stop = makeStop();
    auto huntStartedProm = std::make_shared<std::promise<void>>();
    std::future<void> huntStartedFut = huntStartedProm->get_future();
    auto transport = makeTransport(sock, kUnreachableIp, kDiscardPort, stop, huntStartedProm);

    auto huntStartedAt = std::chrono::steady_clock::time_point{};
    std::future<bool> fut = std::async(std::launch::async, [&]() {
        huntStartedAt = std::chrono::steady_clock::now();
        return transport->enterHuntingState();
    });

    awaitHuntStarted(huntStartedFut);

    auto stopAt = std::chrono::steady_clock::now();
    transport->requestStop();

    auto status = fut.wait_for(std::chrono::milliseconds(kPromptStopMs + 1000));
    ASSERT_EQ(status, std::future_status::ready)
        << "hunt did not return within " << (kPromptStopMs + 1000)
        << "ms of requestStop — cancellability contract violated (hang?)";
    auto returnedAt = std::chrono::steady_clock::now();

    long postStop = elapsedMs(stopAt, returnedAt);
    long totalDuration = elapsedMs(huntStartedAt, returnedAt);

    bool result = fut.get();
    EXPECT_FALSE(result) << "cancelled hunt must report no connection (false)";
    EXPECT_LE(postStop, kPromptStopMs)
        << "hunt returned " << postStop << "ms after requestStop; prompt-stop "
        << "bound is " << kPromptStopMs << "ms";
    // LOAD-BEARING: total lifetime below one backoff cycle => exhaustion
    // impossible => requestStop is the only explanation for the return.
    EXPECT_LT(totalDuration, kExhaustionImpossibleBelowMs)
        << "total hunt duration " << totalDuration << "ms is >= one backoff "
        << "cycle (" << kExhaustionImpossibleBelowMs
        << "ms); exhaustion can no longer be ruled out, so requestStop being "
        << "load-bearing is NOT proven by this run";
    EXPECT_EQ(transport->host_, std::string(kUnreachableIp))
        << "host_ must NOT switch — holds by construction with the no-op "
        << "discovery factory (a switch here would mean a non-injected listener "
        << "was used, i.e. the seam is not wired";
}

// Spec 4: connect-to-unreachable returns false cleanly — no crash, no hang, no
// throw. Driven to give up via requestStop (not retry exhaustion) for speed.
TEST(TCPTransportHuntingCancelTest, ConnectToUnreachableReturnsFalseCleanly) {
    test::FakeSocket sock;
    sock.enqueue(kUnreachableIp, test::failConnect());

    auto stop = makeStop();
    auto huntStartedProm = std::make_shared<std::promise<void>>();
    std::future<void> huntStartedFut = huntStartedProm->get_future();
    auto transport = makeTransport(sock, kUnreachableIp, kDiscardPort, stop, huntStartedProm);

    std::future<bool> fut = std::async(std::launch::async, [&]() {
        return transport->enterHuntingState();
    });

    awaitHuntStarted(huntStartedFut);
    transport->requestStop();

    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(kPromptStopMs + 1000)),
              std::future_status::ready)
        << "hunt did not return cleanly after requestStop (hang?)";
    EXPECT_NO_THROW({
        bool result = fut.get();
        EXPECT_FALSE(result) << "unreachable peer must yield false, not a connection";
    });
}

#endif // VEHICLE_SIM_HUNTING_ENABLED
