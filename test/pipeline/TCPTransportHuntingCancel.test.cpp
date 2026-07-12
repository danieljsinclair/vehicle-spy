// TCPTransportHuntingCancel.test.cpp
//
// BLIND SPEC-FIRST TDD for the CANCELLABLE-CONNECT contract of
// TCPTransport::enterHuntingState() (src/pipeline/TCPTransport.cpp).
//
// This file is the S3776 BLIND-TDD GATE for the #16 hunting-hang fix. It pins
// the cancellability contract ONLY — derived from the public contract
// (TCPTransport.h, StopToken.h) + the relayed behavior spec. It does NOT read
// TCPTransport.cpp (no reading of enterHuntingState/connectAndAuth impl).
//
// CONTRACT BEING PINNED:
//   - StopToken::requestStop() sets stopRequested() (atomic store).
//   - TCPTransport::requestStop() delegates to stop_->requestStop().
//   - enterHuntingState() retries old-IP connect + listens for UDP discovery,
//     returns false if no connection is found.
//   - CANCELLABILITY: requestStop() must interrupt an in-flight hunt
//     (the connect-attempt cycle AND the backoff/retry sleep) PROMPTLY — i.e.
//     the hunt returns within a small bound AFTER requestStop, not after
//     running to retry exhaustion (MAX_RETRIES=60, backoff up to 10s each).
//
// These tests complement (do not duplicate) TCPTransportHunting.test.cpp, which
// covers old-IP-wins / discovery-wins / neither-gives-up / same-ip-ignored. The
// cancellability contract — prompt interruption + the load-bearing proof — is
// NOT asserted there and is the S3776 gate's contribution here.
//
// HERMETIC / DETERMINISTIC via the IDiscoveryListener seam (tech-arch #22): the
// TCPTransport ctor takes an optional DiscoveryListenerFactory (7th param). We
// inject a NO-OP listener (start()→true, poll()→{} after sleeping the timeout,
// stop()→no-op) so the discovery-win path is UNREACHABLE in-test. The hunt's
// only remaining win-paths are requestStop and self-exhaustion — two variables,
// not three. This makes the test HERMETIC: no dependency on the live ESP32 at
// 192.168.68.60 (which broadcasts real UDP discovery on port 3335 and previously
// pre-empted the hunt ~1/60 runs, a flake no wall-clock bound could fix).
//
// LOAD-BEARING PROOF (Spec 1, by elimination): with discovery unreachable, the
// hunt can return false for only TWO reasons: (a) requestStop interrupted it,
// (b) it self-exited via exhaustion. Self-exhaustion requires at least ONE
// backoff sleep of >= BASE_RETRY_DELAY_MS before returning false. So a TOTAL
// hunt lifetime < BASE_RETRY_DELAY_MS (one backoff cycle) cannot be exhaustion
// — leaving requestStop as the only explanation. host_ == kUnreachableIp is now
// belt-and-braces: it holds BY CONSTRUCTION (no-op discovery can never switch
// it), not by environmental luck.
//
// HISTORY: an earlier draft had a separate "without requestStop, the hunt stays
// running past 3s" control test. It flaked (~1/60) — its early exits were real
// discovery wins, not self-exhaustion. The seam (not the bounds) is what makes
// the test reliable; the elimination proof replaces the flaky control.
//
// Same precondition as TCPTransportHunting.test.cpp: enterHuntingState() must
// be compiled/linked on the host build (VEHICLE_SIM_HUNTING_ENABLED is defined
// in the test target). The tests are inert without it.

#include "vehicle-sim/discovery/IDiscoveryListener.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;

// Expose the private enterHuntingState() and host_ for direct test invocation.
// (Mirrors TCPTransportHunting.test.cpp; #pragma once makes the re-include a
// no-op for the definitions, and VEHICLE_SIM_HUNTING_ENABLED already makes
// these two members public in the header — the define-private trick is belt-
// and-braces so the file still compiles if that guard changes.)
#define private public
#include "vehicle-sim/pipeline/TCPTransport.h"
#undef private

#if defined(VEHICLE_SIM_HUNTING_ENABLED)

namespace {

// Non-bindable local IP: connect() here can never succeed (RST/no listener), so
// the old-IP retry path can never win — the hunt stays in its retry/backoff
// loop until cancelled or exhausted.
constexpr const char* kUnreachableIp = "127.0.0.2";
// Discard port: no listener, connect fails promptly with ECONNREFUSED.
constexpr int kDiscardPort = 9;

std::shared_ptr<StopToken> makeStop() { return std::make_shared<StopToken>(); }

// SilentOutput (from ITransportOutput.h) discards all transport log output,
// keeping the test output clean — reuse it instead of a duplicate test sink.
std::shared_ptr<ITransportOutput> quietOutput() {
    return std::make_shared<SilentOutput>();
}

// No-op IDiscoveryListener: the hermetic seam. start() always succeeds;
// poll() returns no devices after a SMALL fixed slice; stop() is a no-op.
// Injected via the TCPTransport ctor's DiscoveryListenerFactory so the hunt's
// discovery-win path is unreachable and the test is deterministic regardless
// of any real ESP32 broadcasting on the LAN.
//
// poll() sleeps a small slice (not the caller's requested timeout, not zero):
//   - NOT zero, so the hunt does not CPU-spin busy-looping on poll() between
//     its own stop-checks;
//   - NOT the full requested timeout, because the no-op has no access to the
//     StopToken and could not otherwise be interrupted — a long blind sleep
//     would make requestStop()'s in-flight interruption appear slow. Returning
//     early-with-empty is contract-compliant ("wait UP TO timeout") and keeps
//     the no-op out of the cancellation-latency path: requestStop is observed
//     by the hunt's OWN stop-check between polls, within one slice.
constexpr int kNoOpPollSliceMs = 10;
class NoOpDiscoveryListener : public vehicle_sim::discovery::IDiscoveryListener {
public:
    bool start() override { return true; }
    std::vector<vehicle_sim::discovery::DiscoveredDevice> poll(
        std::chrono::milliseconds /*timeout*/) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(kNoOpPollSliceMs));
        return {};
    }
    void stop() override {}
};

// Factory returning a fresh no-op listener — passed as the TCPTransport ctor's
// 7th argument (DiscoveryListenerFactory).
DiscoveryListenerFactory noOpDiscoveryFactory() {
    return []() { return std::make_unique<NoOpDiscoveryListener>(); };
}

// Timing bounds for the cancellability contract. All anchored to PUBLIC
// constexprs on TCPTransport (header contract) — no impl assumptions.
//
//   - kPromptStopMs: the hunt must return within this AFTER requestStop. 2000ms
//     is generous for any reasonable connect+sleep interruption (a single
//     backoff tick is at least BASE_RETRY_DELAY_MS=1000ms; a cancelled hunt
//     that respects the stop between ticks returns well under 2s). Used as a
//     hang-guard via wait_for so a regression to blocking-connect fails fast.
//
//   - kExhaustionImpossibleBelowMs: the elimination floor for the load-bearing
//     proof (see Spec 1). Bound to BASE_RETRY_DELAY_MS (1000ms) — the minimum
//     duration of ONE backoff-and-retry cycle. If the hunt returns in TOTAL
//     time < this floor, it cannot have completed even a single retry cycle,
//     so it cannot have self-exited via exhaustion. Combined with "discovery
//     did not fire" (host_ unchanged), the ONLY remaining explanation for a
//     prompt return is that requestStop interrupted it. This makes Spec 1 the
//     load-bearing proof BY ELIMINATION — no wall-clock race against the
//     schedule, no second "control" test that itself flakes.
constexpr int kPromptStopMs = 2000;
// Elimination floor = one backoff cycle. ASSERT-able because BASE_RETRY_DELAY_MS
// is the documented minimum retry delay (TCPTransport.h:115); any exhaustion
// path must sleep at least this once before returning false.
constexpr int kExhaustionImpossibleBelowMs = TCPTransport::BASE_RETRY_DELAY_MS;

// Wait until the async hunt has observably started (it has entered
// enterHuntingState and is mid-loop), then return. Polls the started flag.
void awaitHuntStarted(std::atomic<bool>& started) {
    while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Yield one extra slice so the hunt is genuinely inside its loop (connect
    // attempt / backoff sleep) before we assert on it.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// Elapsed milliseconds between two steady_clock time points.
long elapsedMs(std::chrono::steady_clock::time_point from,
               std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

} // namespace

// ===========================================================================
// Spec 1: requestStop() interrupts the hunt PROMPTLY — AND (via the
// elimination argument below) this single test is the LOAD-BEARING proof that
// requestStop is what interrupts, not self-exhaustion.
//
// Observable assertions:
//   - postStop      <= kPromptStopMs       : the hunt returns promptly AFTER
//                                            requestStop (hang-guard via wait_for).
//   - totalDuration <  kExhaustionImpossibleBelowMs : the TOTAL hunt lifetime
//                                            (start → return) is shorter than
//                                            ONE backoff cycle.
//   - result == false                      : no connection won.
//   - host_ == kUnreachableIp              : belt-and-braces — holds BY
//                                            CONSTRUCTION (no-op discovery).
//
// ELIMINATION ARGUMENT (two-variable, discovery removed by construction):
//   With the no-op discovery factory injected, the hunt can return false for
//   only TWO reasons:
//     (a) requestStop interrupted it  — what we want to prove works,
//     (b) it self-exited via exhaustion / retry-limit.
//   (b) requires at least ONE backoff sleep of >= BASE_RETRY_DELAY_MS (the
//   documented minimum retry delay) before returning false — so any return
//   with totalDuration < BASE_RETRY_DELAY_MS cannot be exhaustion. With (b)
//   eliminated, (a) is the only explanation: requestStop IS load-bearing.
//   (Discovery is not a third variable here: the no-op listener can never
//   produce a device, so it can never cause a return or switch host_.)
//
// ASSUMPTION TO CONFIRM IN VIEW (documented, not suppressed): the elimination
// holds IF tech-arch's enterHuntingState does not fast-exit on first
// connect-refused WITHOUT a backoff (i.e. a self-exit path must sleep >=
// BASE_RETRY_DELAY_MS at least once). If a future refactor adds such a
// fast-exit, this floor must be re-derived. Flagged for tech-arch.
// ===========================================================================
TEST(TCPTransportHuntingCancelTest, RequestStopInterruptsAndIsLoadBearing) {
    auto stop = makeStop();
    TCPTransport transport(kUnreachableIp, kDiscardPort, "raw",
                           quietOutput(), TcpReadTiming{}, stop,
                           noOpDiscoveryFactory());

    // Capture the hunt's TRUE start time inside the async task (the first
    // statement before enterHuntingState) so totalDuration is measured from the
    // actual hunt entry, not from an external poll.
    auto huntStartedAt = std::chrono::steady_clock::time_point{};
    std::atomic<bool> started{false};
    std::future<bool> fut = std::async(std::launch::async, [&]() {
        huntStartedAt = std::chrono::steady_clock::now();
        started.store(true);
        return transport.enterHuntingState();
    });

    awaitHuntStarted(started);

    auto stopAt = std::chrono::steady_clock::now();
    transport.requestStop();

    // Hang-guard: if requestStop is not respected (#16 bug), the hunt runs to
    // retry exhaustion (tens of seconds). wait_for fails the test FAST instead
    // of wedging the suite / blowing the ctest timeout.
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
    EXPECT_EQ(transport.host_, std::string(kUnreachableIp))
        << "host_ must NOT switch — holds by construction with the no-op "
        << "discovery factory (a switch here would mean a non-injected listener "
        << "was used, i.e. the seam is not wired)";
}

// ===========================================================================
// Spec 4: connect-to-unreachable returns false cleanly — no crash, no hang, no
// throw. Drives the hunt to give up via requestStop (not retry exhaustion, to
// keep the test fast) and asserts the outcome is a clean false with no
// exception escaping the async task.
// ===========================================================================
TEST(TCPTransportHuntingCancelTest, ConnectToUnreachableReturnsFalseCleanly) {
    auto stop = makeStop();
    TCPTransport transport(kUnreachableIp, kDiscardPort, "raw",
                           quietOutput(), TcpReadTiming{}, stop,
                           noOpDiscoveryFactory());

    std::atomic<bool> started{false};
    std::future<bool> fut = std::async(std::launch::async, [&]() {
        started.store(true);
        return transport.enterHuntingState();
    });

    awaitHuntStarted(started);
    transport.requestStop();

    // get() rethrows any exception that escaped enterHuntingState. Asserting
    // no-throw + a timely ready state pins "clean" (no hang, no crash, no
    // exception). A hang manifests as a timeout on wait_for.
    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(kPromptStopMs + 1000)),
              std::future_status::ready)
        << "hunt did not return cleanly after requestStop (hang?)";
    EXPECT_NO_THROW({
        bool result = fut.get();
        EXPECT_FALSE(result) << "unreachable peer must yield false, not a connection";
    });
}

#endif // VEHICLE_SIM_HUNTING_ENABLED
