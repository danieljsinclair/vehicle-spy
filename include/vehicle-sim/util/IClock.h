#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace vehicle_sim::util {

/**
 * Clock abstraction for dependency injection into threaded loops.
 *
 * THE clock abstraction for this project — there is exactly one. It replaces
 * the old domain/ITimeProvider (now deleted), which only exposed a nowMs()
 * reader and therefore could not unblock a thread parked in
 * std::condition_variable::wait_until.
 *
 * Why waitFor() exists (and not just now()):
 *   Advancing a fake now() does NOT by itself wake a thread blocked in
 *   cv.wait_until — that call parks on the OS until real wall-clock time
 *   passes or the cv is notified. IClock::waitFor() bridges that gap:
 *     - SystemClock forwards to the real cv.wait_until (OS-level blocking).
 *     - FakeClock never parks: it spins on the predicate, advancing its own
 *       virtual time in step with the producer's advance() calls, and returns
 *       the moment the deadline is reached or the predicate is satisfied.
 *   This gives deterministic, wall-clock-free tests of tick loops that do
 *   `clock->waitFor(cv, lock, pred, clock->now() + interval)`.
 *
 * Consumers: threaded tick loops (e.g. DemoSignalProvider's). They should
 * call clock->waitFor(...), NEVER a bare cv.wait_until(...) with a clock
 * deadline, so that an injected FakeClock makes the loop deterministic.
 */
class IClock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    virtual ~IClock() = default;

    /**
     * Current instant of this clock's virtual (or real) time.
     *
     * Thread-safe; monotonically non-decreasing for a given instance.
     */
    [[nodiscard]] virtual time_point now() const = 0;

    /**
     * Sleep (park the calling thread) for `d` of THIS clock's time.
     *
     * For a SystemClock this is real wall-clock sleep (std::this_thread::
     * sleep_for) — production behavior, unchanged. For a FakeClock this must
     * NOT park on the OS wall clock (a test would then be real-time-bound): the
     * FakeClock implementation advances its virtual time by `d` and returns
     * immediately, so callers that sleep on a FakeClock stay deterministic and
     * instant. The contract is "the caller yields for `d` of clock time"; a
     * FakeClock honours that by moving virtual time forward, not by blocking.
     *
     * Consumers: TCPTransport's hunt backoff + its handshake pacing, which
     * sleep for fixed durations and do NOT need a condition_variable to wake on
     * (unlike waitFor). Kept as a separate primitive from waitFor() because the
     * backoff loop re-checks a stop predicate each slice itself.
     */
    virtual void sleepFor(std::chrono::milliseconds d) = 0;

    /**
     * Block the calling thread until `deadline` is reached on THIS clock OR
     * `pred` becomes true, whichever is first.
     *
     * Equivalent to cv.wait_until(lock, deadline, pred) but clock-aware:
     * for a FakeClock the caller is released ~promptly when another thread's
     * advance() moves virtual time past `deadline`.
     *
     * @param cv       condition_variable to wait on (caller retains ownership).
     * @param lock     unique_lock the caller holds; passed by reference.
     * @param pred     wake-early predicate; returns true to stop waiting.
     * @param deadline absolute time_point on this clock to wait until.
     * @return         true if pred() holds on wake; false if deadline elapsed.
     */
    template <class Predicate>
    bool waitFor(std::condition_variable& cv,
                 std::unique_lock<std::mutex>& lock,
                 Predicate pred,
                 time_point deadline) const {
        return waitForImpl(cv, lock, std::function<bool()>(std::move(pred)),
                           deadline);
    }

protected:
    // Non-template virtual seam: lets FakeClock override the waiting strategy
    // without templated dispatch (which can't be virtual). Concrete clocks
    // implement this; the public waitFor() wraps predicates in std::function.
    [[nodiscard]] virtual bool waitForImpl(
        std::condition_variable& cv,
        std::unique_lock<std::mutex>& lock,
        const std::function<bool()>& pred,
        time_point deadline) const = 0;
};

/**
 * Production clock backed by std::chrono::steady_clock.
 *
 * now() reads the OS monotonic clock; waitFor() forwards to the real
 * cv.wait_until, so blocking is real wall-clock time.
 */
class SystemClock final : public IClock {
public:
    [[nodiscard]] time_point now() const override;

    void sleepFor(std::chrono::milliseconds d) override;

protected:
    [[nodiscard]] bool waitForImpl(
        std::condition_variable& cv,
        std::unique_lock<std::mutex>& lock,
        const std::function<bool()>& pred,
        time_point deadline) const override;
};

/**
 * Deterministic clock for tests.
 *
 * Virtual time that only moves when advance() is called. now() is
 * thread-safe and non-decreasing.
 *
 * RACE-FREE WAKEUP DESIGN (no polling):
 *   A consumer thread parks inside waitForImpl on the CONSUMER's own
 *   condition_variable (the one it passed in), holding the CONSUMER's own
 *   lock — exactly the cv/lock the consumer uses for its shared state and
 *   its stop() path. While parking, waitForImpl REGISTERS that (cv, lock)
 *   pair with the FakeClock (guarded by FakeClock::mutex_).
 *
 *   advance(), from any thread, then:
 *     1. takes FakeClock::mutex_, copies out the registered (cv, lock*),
 *        bumps now_, releases FakeClock::mutex_;
 *     2. if a consumer is registered, takes the CONSUMER's lock, then
 *        notifies the CONSUMER's cv.
 *
 *   Because the waiter holds the consumer lock across BOTH its predicate
 *   check AND its cv.wait(), and advance() takes that SAME consumer lock to
 *   do bump+notify, the two are fully serialized — there is no window in
 *   which a notify can be lost (lost-wakeup), and the wakeup always lands on
 *   the cv the waiter is actually parked on (no wrong-cv). Likewise a
 *   consumer stop() that takes its own lock and notifies its own cv is
 *   guaranteed to reach the waiter.
 *
 *   now_ is therefore mutated and predicate-read only while the consumer
 *   lock is held (during a parked wait), so there is no data race on now_
 *   along the wait path. The standalone now() reader still takes
 *   FakeClock::mutex_ for the no-waiter / pre-registration case.
 *
 * IMPORTANT: consumers must wait via clock->waitFor(...) — which FakeClock
 * intercepts — rather than a raw cv.wait_until(...), so the wakeup above can
 * fire. FakeClock::waitForImpl never parks on the OS wall clock.
 *
 * SINGLE-CONSUMER CONTRACT: a given FakeClock supports at most one
 * concurrently-parked waiter (one consumer (cv, lock) pair registered at a
 * time). This matches how the tests use it (one worker per FakeClock).
 * Registering a second waiter while the first is parked is undefined.
 */
class FakeClock final : public IClock {
public:
    FakeClock() noexcept = default;

    /** Start virtual time at a non-zero base (e.g. to avoid epoch edge cases). */
    explicit FakeClock(time_point initial) noexcept;

    [[nodiscard]] time_point now() const override;

    void sleepFor(std::chrono::milliseconds d) override;

    /**
     * Advance virtual time by `d` and wake any waiter parked in waitFor().
     * Monotonic: d must be non-negative (negative advance is a no-op).
     *
     * @warning Do NOT call advance() while holding the consumer lock that was
     *   passed to waitFor(). To wake a parked waiter race-free, advance()
     *   acquires that consumer lock; if the caller already holds it, advance()
     *   self-deadlocks (it blocks forever waiting for a lock it already owns).
     *   In practice advance() is driven from the test/producer thread, never
     *   from inside a consumer's locked critical section (e.g. never under the
     *   same lock whose waitFor() this advance() is meant to release). This is
     *   a silent deadlock with no compile-time guard — observe the contract.
     */
    void advance(duration d);

protected:
    [[nodiscard]] bool waitForImpl(
        std::condition_variable& cv,
        std::unique_lock<std::mutex>& lock,
        const std::function<bool()>& pred,
        time_point deadline) const override;

private:
    // Guards now_ and the registered-consumer pointer. Does NOT guard the
    // consumer's own shared state — that is the consumer's lock's job.
    mutable std::mutex mutex_;

    // The (cv, lock) of the consumer currently parked in waitForImpl, if any.
    // advance() uses this to wake the waiter on the cv it is actually parked
    // on. Set under mutex_ by waitForImpl before parking; cleared under
    // mutex_ after waking. Null when no waiter is parked.
    mutable std::condition_variable* registeredCv_{nullptr};
    mutable std::unique_lock<std::mutex>* registeredLock_{nullptr};

    time_point now_{};
};

} // namespace vehicle_sim::util
