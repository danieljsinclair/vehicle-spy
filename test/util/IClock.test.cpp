#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "vehicle-sim/util/IClock.h"

using namespace vehicle_sim::util;

using namespace std::chrono_literals;

namespace {

// Shared state the worker loop protects with its own mutex, mirroring how a
// real tick loop (e.g. DemoSignalProvider) would gate its condition_variable.
struct WorkerState {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> ready{false};
    std::atomic<bool> deadlineReached{false};
    std::atomic<std::chrono::steady_clock::time_point> wokeAt{};
};

// Worker that blocks on the injected clock's waitFor until the deadline.
// Mirrors the production pattern: cv.wait_until(lock, clock->now()+d, pred).
// Using clock->waitFor() is what lets a FakeClock release it deterministically.
template <class Clock>
void waitOnClock(Clock& clock, WorkerState& s, IClock::duration interval) {
    std::unique_lock<std::mutex> lock(s.mutex);
    const auto deadline = clock.now() + interval;
    s.ready.store(true, std::memory_order_release);
    const bool ok = clock.waitFor(
        s.cv, lock, [&] { return s.deadlineReached.load(); }, deadline);
    (void)ok; // We assert wake-up *promptness*, not the bool result, here.
    s.wokeAt.store(std::chrono::steady_clock::now(), std::memory_order_release);
}

} // namespace

// ---------------------------------------------------------------------------
// SystemClock
// ---------------------------------------------------------------------------

TEST(SystemClockTest, NowIsNonDecrecreasing) {
    SystemClock clock;
    const auto a = clock.now();
    const auto b = clock.now();
    EXPECT_LE(a, b);
}

TEST(SystemClockTest, NowTracksRealWallClock) {
    // SystemClock::now() must be within a small skew of steady_clock::now().
    SystemClock clock;
    const auto real = std::chrono::steady_clock::now();
    const auto measured = clock.now();
    const auto skew = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::abs(measured - real));
    EXPECT_LT(skew, std::chrono::milliseconds(50));
}

TEST(SystemClockTest, WaitForReturnsPromptlyWhenPredicateTrue) {
    SystemClock clock;
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(m);
    const auto deadline = clock.now() + std::chrono::seconds(60);
    // Predicate already true: must NOT block for 60s.
    const auto start = std::chrono::steady_clock::now();
    const bool result = clock.waitFor(cv, lock, [] { return true; }, deadline);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    EXPECT_TRUE(result);
    EXPECT_LT(elapsed, std::chrono::milliseconds(50));
}

// ---------------------------------------------------------------------------
// FakeClock
// ---------------------------------------------------------------------------

TEST(FakeClockTest, NowStartsAtEpochByDefault) {
    FakeClock clock;
    EXPECT_EQ(clock.now(), IClock::time_point{});
}

TEST(FakeClockTest, AdvanceArithmeticIsMonotonicAndAdditive) {
    FakeClock clock;
    const auto before = clock.now();
    clock.advance(100ms);
    EXPECT_EQ(clock.now() - before, 100ms);
    clock.advance(250ms);
    EXPECT_EQ(clock.now() - before, 350ms);
}

TEST(FakeClockTest, AdvanceByZeroOrNegativeIsNoOp) {
    FakeClock clock;
    const auto t0 = clock.now();
    clock.advance(IClock::duration::zero());
    EXPECT_EQ(clock.now(), t0);
    clock.advance(IClock::duration{-5});
    EXPECT_EQ(clock.now(), t0);
}

TEST(FakeClockTest, NowIsThreadSafeUnderConcurrentAdvance) {
    // Hammer advance() from multiple threads; now() must stay consistent and
    // monotonic across reads. If locking were broken we'd see torn reads or
    // regression — this is a smoke test of the mutex guard.
    FakeClock clock;
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                clock.advance(1ms);
            }
        });
    }
    IClock::time_point prev{};
    for (int i = 0; i < 1000; ++i) {
        const auto n = clock.now();
        EXPECT_GE(n, prev);
        prev = n;
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& t : threads) {
        t.join();
    }
}

// ---------------------------------------------------------------------------
// The CRITICAL test: a worker blocked on waitFor(now()+100ms) is released
// ~promptly (within 10ms) after another thread calls advance(100ms).
// ---------------------------------------------------------------------------

TEST(FakeClockTest, AdvanceReleasesBlockedWaitForWithin10ms) {
    FakeClock clock;
    WorkerState state;

    std::thread worker([&] {
        waitOnClock(clock, state, 100ms);
    });

    // Wait until the worker is actually parked in waitFor before advancing.
    while (!state.ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    // Tiny settle so the worker has entered the cv wait.
    std::this_thread::sleep_for(2ms);

    const auto advanceStart = std::chrono::steady_clock::now();
    clock.advance(100ms);

    // Worker must wake promptly.
    worker.join();
    const auto wokeAt = state.wokeAt.load(std::memory_order_acquire);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        wokeAt - advanceStart);

    EXPECT_LE(elapsed, 10ms)
        << "Worker woke " << elapsed.count()
        << "ms after advance — expected <= 10ms";
}

TEST(FakeClockTest, WaitForReturnsFalseWhenPredicateNeverTrueAndDeadlinePasses) {
    // Predicate stays false; advance past the deadline -> waitFor returns
    // false promptly (does NOT block forever).
    FakeClock clock;
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(m);
    const auto deadline = clock.now() + 50ms;

    std::thread advancer([&clock] {
        std::this_thread::sleep_for(5ms);
        clock.advance(50ms); // pushes virtual time past the deadline
    });

    const auto start = std::chrono::steady_clock::now();
    const bool result = clock.waitFor(cv, lock, [] { return false; }, deadline);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    advancer.join();

    EXPECT_FALSE(result);
    EXPECT_LT(elapsed, 100ms); // woke shortly after advance, not after 50ms real
}

TEST(FakeClockTest, WaitForRespectsPredicateWakeupBeforeDeadline) {
    // If the predicate flips true before the deadline, waitFor returns true
    // early even on a fake clock. The flip is done the way a REAL consumer
    // does it (e.g. DemoSignalProvider::stop): acquire the shared lock, set
    // the flag, RELEASE the lock, then notify the shared cv. This is the cv
    // contract the race-free FakeClock enforces: the waiter parks on the
    // consumer's own cv, so the notify-on-that-cv is what releases it.
    //
    // NOTE: we must NOT call advance() while holding the consumer lock here —
    // under the race-free design advance() acquires that SAME consumer lock to
    // bump now_+notify, so calling it under a held consumer lock would
    // self-deadlock. The notify alone is sufficient and is the correct pattern.
    FakeClock clock;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> flag{false};
    const auto deadline = clock.now() + std::chrono::seconds(60);

    std::thread setter([&] {
        std::this_thread::sleep_for(5ms);
        {
            std::lock_guard<std::mutex> g(m);
            flag.store(true, std::memory_order_release);
        }
        cv.notify_all();
    });

    std::unique_lock<std::mutex> lock(m);
    const bool result =
        clock.waitFor(cv, lock, [&] { return flag.load(); }, deadline);
    setter.join();

    EXPECT_TRUE(result);
}

// De-risks the DemoSignalProvider shutdown path: a worker parked in a FAR-future
// waitFor (predicate initially false, simulating a running tick loop) must be
// released promptly once stop() flips running_ to false. This mirrors the race
// where stop() and a blocked tick loop contend — the wait must NOT linger until
// the 60s deadline, and must report that the predicate (now true) is satisfied.
TEST(FakeClockTest, StopRacingBlockedWaitForWakesPromptly) {
    FakeClock clock;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> running{true}; // pred mirrors `running_` in the provider
    const auto deadline = clock.now() + 60000ms; // far-future, would block alone

    std::thread stopper([&] {
        // Let the worker enter the wait first.
        std::this_thread::sleep_for(5ms);
        // stop(): flip running_ to false under the shared lock and notify.
        // This notify goes to the CONSUMER's cv — the one waitForImpl is
        // parked on in the race-free design. (The old band-aid parked on
        // FakeClock's internalCv_ and so REQUIRED an advance() to wake; here
        // the bare consumer-cv notify must reach the waiter on its own.)
        std::lock_guard<std::mutex> g(m);
        running.store(false, std::memory_order_release);
        cv.notify_all();
    });

    const auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(m);
    const bool result =
        clock.waitFor(cv, lock, [&] { return !running.load(); }, deadline);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    stopper.join();

    EXPECT_TRUE(result);                  // predicate (stop) became true
    EXPECT_LT(elapsed, 50ms);             // released promptly, not at the 60s deadline
}

// ---------------------------------------------------------------------------
// REGRESSION: lost-wakeup stress. The original FakeClock parked on its OWN
// internal cv under the consumer's lock with a 2ms wait_for fallback, because
// advance() (which bumped now_ under a DIFFERENT mutex and notified the
// internal cv) could fire in the window between the predicate check and the
// park — a classic lost wakeup that the 2ms poll papered over. With the poll
// removed and the wait parked on the consumer cv (serialized with advance's
// bump+notify under the consumer lock), this loop must complete promptly and
// deterministically. If the race returns, the waiter parks with no wakeup and
// the loop hangs until the real-time bound trips the assertion.
// ---------------------------------------------------------------------------
TEST(FakeClockTest, ConcurrentAdvanceNeverLosesWakeupUnderStress) {
    constexpr int kIterations = 1000;
    constexpr auto kRealTimeBudget = std::chrono::seconds(2);

    const auto suiteStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        FakeClock clock;
        std::mutex m;
        std::condition_variable cv;
        std::atomic<bool> waiterDone{false};

        std::thread waiter([&] {
            std::unique_lock<std::mutex> lock(m);
            const auto deadline = clock.now() + 5ms;
            // Predicate never becomes true; the ONLY legitimate release is
            // advance() pushing virtual time past the deadline. If the wakeup
            // is lost, this parks forever (until the test budget trips).
            clock.waitFor(cv, lock, [] { return false; }, deadline);
            waiterDone.store(true, std::memory_order_release);
        });

        // Hammer advance() repeatedly until the waiter is observed released.
        // On the buggy code, at least one of the 1000 iterations will hit the
        // lost-wakeup window and the waiter will never be observed done within
        // the per-iteration bound, tripping the assertion.
        const auto iterStart = std::chrono::steady_clock::now();
        while (!waiterDone.load(std::memory_order_acquire)) {
            clock.advance(1ms);
            if (std::chrono::steady_clock::now() - iterStart > kRealTimeBudget) {
                FAIL() << "Lost wakeup on iteration " << i
                       << ": waiter never released by advance() within "
                       << kRealTimeBudget.count() << "s";
            }
        }
        waiter.join();

        // Hard bound on the whole suite too — a subtle leak that adds a few ms
        // per iteration would blow this.
        const auto totalElapsed =
            std::chrono::steady_clock::now() - suiteStart;
        ASSERT_LT(totalElapsed, std::chrono::seconds(10))
            << "Stress loop exceeded 10s total after " << i << " iterations";
    }
    SUCCEED();
}
