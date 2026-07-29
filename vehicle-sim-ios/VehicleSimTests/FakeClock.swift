import Foundation

// MARK: - FakeClock

/// A deterministic, non-real-time clock for tests.
///
/// `FakeClock` is a pure counter: time only advances when the test calls
/// `advance(_:)`. No threads, no timers, no real wall-clock reads. This makes
/// time-based state machines (backoff, liveness, cancel transitions) fully
/// deterministic and race-free.
final class FakeClock {
    private(set) var now: Int = 0

    /// Advance the clock by `ms` milliseconds.
    func advance(_ ms: Int) {
        now += ms
    }

    /// Reset the clock to zero.
    func reset() {
        now = 0
    }
}

// MARK: - VMClock

/// Protocol abstracting millisecond wall-clock reads so production code can be
/// tested with `FakeVMClock` instead of real time.
protocol VMClock {
    func nowMs() -> UInt64
}

// MARK: - WallVMClock

/// Production `VMClock` backed by the real system clock.
struct WallVMClock: VMClock {
    func nowMs() -> UInt64 {
        UInt64(Date().timeIntervalSince1970 * 1000)
    }
}

// MARK: - FakeVMClock

/// Test `VMClock` backed by `FakeClock`'s deterministic counter.
final class FakeVMClock: VMClock {
    private(set) var now: UInt64 = 0

    func nowMs() -> UInt64 {
        now
    }

    /// Advance the fake clock by `ms` milliseconds.
    func advance(_ ms: UInt64) {
        now += ms
    }

    /// Reset the fake clock to zero.
    func reset() {
        now = 0
    }
}
