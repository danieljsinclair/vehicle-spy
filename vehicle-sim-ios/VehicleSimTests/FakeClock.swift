import Foundation
@testable import VehicleSim

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

// MARK: - FakeVMClock

/// Test `VMClock` backed by a deterministic counter.
///
/// `FakeVMClock` implements the `VMClock` protocol (defined in the production
/// `VehicleSim` module) so it can be injected into `VehicleViewModel` in tests.
/// Time only advances when the test calls `advance(_:)`.
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
