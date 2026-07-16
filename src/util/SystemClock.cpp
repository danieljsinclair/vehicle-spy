#include "vehicle-sim/util/IClock.h"
#include <cassert>
#include <thread>

namespace vehicle_sim::util {

// ---------------------------------------------------------------------------
// SystemClock
// ---------------------------------------------------------------------------

IClock::time_point SystemClock::now() const {
    return std::chrono::steady_clock::now();
}

void SystemClock::sleepFor(std::chrono::milliseconds d) {
    // Production path: real wall-clock sleep. Unchanged from the pre-seam
    // std::this_thread::sleep_for used by TCPTransport's backoff + pacing.
    std::this_thread::sleep_for(d);
}

bool SystemClock::waitForImpl(
    std::condition_variable& cv,
    std::unique_lock<std::mutex>& lock,
    const std::function<bool()>& pred,
    time_point deadline) const {
    // Production path: park on the OS until real wall-clock time reaches the
    // deadline or the predicate is satisfied / the cv is notified.
    if (pred()) {
        return true;
    }
    if (cv.wait_until(lock, deadline, [&pred]() { return pred(); })) {
        return true;
    }
    // wait_until returned false: deadline elapsed without pred becoming true.
    return false;
}

// ---------------------------------------------------------------------------
// FakeClock
// ---------------------------------------------------------------------------

FakeClock::FakeClock(time_point initial) noexcept : now_(initial) {}

void FakeClock::sleepFor(std::chrono::milliseconds d) {
    // Test path: do NOT park on the OS wall clock (that would re-introduce
    // real-time sleeps into otherwise-instant tests). Advance virtual time by
    // `d` and return immediately. This keeps TCPTransport's hunt backoff +
    // handshake pacing deterministic and <1 ms in tests while still honouring
    // the "caller yielded for `d` of clock time" contract: any later now() read
    // (or a parked waitFor whose deadline lay within `d`) sees the advanced time.
    //
    // If a consumer happens to be parked in waitFor() on this clock, bump under
    // its lock and notify (mirrors advance()) so its deadline check fires.
    if (d <= duration::zero()) {
        return;
    }
    std::condition_variable* cv = nullptr;
    std::mutex* consumerMutex = nullptr;
    {
        std::scoped_lock guard(mutex_);
        cv = registeredCv_;
        if (registeredLock_ != nullptr) {
            consumerMutex = registeredLock_->mutex();
        }
    }
    if (cv == nullptr) {
        std::scoped_lock guard(mutex_);
        now_ += d;
        return;
    }
    if (consumerMutex == nullptr) {
        // cv is registered but its lock is not (a brief window where
        // registeredCv_ is set but registeredLock_ is not, or the lock was
        // released): never dereference a possibly-null consumerMutex. Bump
        // now_ under FakeClock::mutex_ only, exactly like the cv==nullptr branch.
        std::scoped_lock guard(mutex_);
        now_ += d;
        return;
    }
    {
        // Document the proven invariant: cv != nullptr guarantees registeredLock_ != nullptr
        assert(consumerMutex != nullptr && "consumerMutex must be non-null when cv is registered");
        std::scoped_lock guard(*consumerMutex, mutex_);
        now_ += d;
    }
    cv->notify_all();
}

IClock::time_point FakeClock::now() const {
    std::scoped_lock guard(mutex_);
    return now_;
}

void FakeClock::advance(duration d) {
    if (d <= duration::zero()) {
        return;
    }

    // Snapshot the registered consumer (cv, lock) under FakeClock::mutex_.
    // Lock order across the whole class is ALWAYS: consumer-lock (if taken)
    // before FakeClock::mutex_. The waiter's predicate runs while holding the
    // consumer lock and reads now_ via now() (which takes mutex_) — same
    // order, so no lock-order inversion / deadlock is possible.
    std::condition_variable* cv = nullptr;
    std::mutex* consumerMutex = nullptr;
    {
        std::scoped_lock guard(mutex_);
        cv = registeredCv_;
        if (registeredLock_ != nullptr) {
            consumerMutex = registeredLock_->mutex();
        }
    }

    if (cv == nullptr) {
        // No waiter parked: bump now_ under FakeClock::mutex_ only.
        std::scoped_lock guard(mutex_);
        now_ += d;
        return;
    }

    // A consumer IS parked (cv != nullptr). Since the waiter registers its cv
    // and lock together atomically under mutex_ (see waitForImpl), cv != nullptr
    // guarantees registeredLock_ != nullptr and that its unique_lock owns its
    // mutex — i.e. consumerMutex is provably non-null here. Take BOTH the
    // consumer lock and FakeClock::mutex_ in a single std::scoped_lock (which
    // applies deadlock-avoidance) so the bump is serialized with the waiter's
    // predicate-check + cv.wait (consumer lock) and consistent with the
    // standalone now() reader (FakeClock::mutex_). The waiter cannot pass its
    // check AND release the lock to re-park in a way that misses our notify,
    // because cv.wait atomically re-checks the predicate on wake under this
    // same consumer lock.
    {
        // Document the proven invariant: cv != nullptr guarantees registeredLock_ != nullptr
        assert(consumerMutex != nullptr && "consumerMutex must be non-null when cv is registered");
        std::scoped_lock guard(*consumerMutex, mutex_);
        now_ += d;
    }
    // Notify on the cv the waiter is actually parked on. Notifying is done
    // without holding either lock (standard cv guidance) — correctness does
    // not depend on it because the waiter's predicated cv.wait re-checks
    // under the consumer lock on wake.
    cv->notify_all();
}

bool FakeClock::waitForImpl(
    std::condition_variable& cv,
    std::unique_lock<std::mutex>& lock,
    const std::function<bool()>& pred,
    time_point deadline) const {
    // Deterministic path: never park on the OS wall clock.
    //
    // We park on the CONSUMER's cv (the one passed in), holding the
    // CONSUMER's lock. Before parking we register (cv, &lock) under
    // FakeClock::mutex_ so advance() can find them and wake us on exactly the
    // cv we are parked on. The pred reads now_ which advance() bumps under
    // this same consumer lock — fully serialized, no lost wakeup, no poll.

    if (pred()) {
        return true;
    }

    // Check if the deadline is already past. If so, return false immediately
    // without parking. This handles the case where the clock is advanced
    // BEFORE the wait starts (e.g., in a test that advances the clock
    // before calling waitForPrompt).
    if (now() >= deadline) {
        return false;
    }

    // Register ourselves as the active waiter. The caller's `lock` is held on
    // entry and remains held; we keep it held across the cv.wait below.
    {
        std::scoped_lock guard(mutex_);
        registeredCv_ = &cv;
        registeredLock_ = &lock;
    }
    // Re-check pred/deadline AFTER registering and WHILE holding the consumer
    // lock, to close the window between the initial check above and the park
    // below. advance() that races here must take the consumer lock to bump
    // now_, so it cannot slip a bump+notify in between our check and our
    // cv.wait — the standard predicated cv.wait(lock, pred) guarantees this.
    cv.wait(lock, [this, &pred, &deadline] {
        return pred() || now() >= deadline;
    });

    // Unregister on the way out (under FakeClock::mutex_).
    {
        std::scoped_lock guard(mutex_);
        registeredCv_ = nullptr;
        registeredLock_ = nullptr;
    }
    return pred();
}

} // namespace vehicle_sim::util
