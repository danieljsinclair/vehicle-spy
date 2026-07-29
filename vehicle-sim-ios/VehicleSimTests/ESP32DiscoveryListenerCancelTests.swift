import Foundation
import XCTest
@testable import VehicleSim

/// Blind red tests for the NWListener async-cancel fix (NWError 48).
///
/// Root cause: `ESP32DiscoveryListener.stop()` calls `cancelListening()` and
/// returns immediately. The underlying `NWListener` reports `.cancelled`
/// asynchronously, so the UDP port is still held when `stop()` returns. If
/// `start()` is called immediately after `stop()`, the new `NWListener` tries
/// to bind the same port → NWError 48 ("Address already in use").
///
/// Fix: `stop()` must wait for `.cancelled` before returning, so the port is
/// fully released before the next `start()` attempts to rebind.
///
/// These tests use `FakeDiscoveryListener` (deterministic state machine driven
/// by `FakeClock`) — no real time, no threads, no real UDP.
final class ESP32DiscoveryListenerCancelTests: XCTestCase {

    private var clock: FakeClock!
    private var fakeListener: FakeDiscoveryListener!
    private var listener: ESP32DiscoveryListener!
    private var discoveredDevices: [DiscoveredESP32] = []
    private var errors: [ESP32DiscoveryListenerError] = []

    override func setUp() {
        super.setUp()
        clock = FakeClock()
        fakeListener = FakeDiscoveryListener(clock: clock)
        discoveredDevices = []
        errors = []
    }

    override func tearDown() {
        // Use waitForCancelled: false in tearDown to avoid blocking during
        // cleanup (no tick() will be called to advance the state machine).
        listener?.stop(waitForCancelled: false)
        listener = nil
        fakeListener = nil
        clock = nil
        super.tearDown()
    }

    private func makeListener() -> ESP32DiscoveryListener {
        ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { [weak self] in self?.discoveredDevices.append($0) },
            onError: { [weak self] in self?.errors.append($0) },
            listener: fakeListener
        )
    }

    // MARK: - 1. cancelListening() → .cancelling immediately

    /// `cancelListening()` must transition the state to `.cancelling` synchronously.
    /// This is the first half of the async-cancel contract: the caller knows
    /// teardown has begun, but the port is not yet released.
    func testCancelTransitionsToCancellingImmediately() throws {
        try fakeListener.startListening(onState: { _ in }, onPacket: { _, _ in })
        XCTAssertEqual(fakeListener.state, .ready)

        fakeListener.cancelListening()

        XCTAssertEqual(fakeListener.state, .cancelling,
                       "cancelListening() must transition to .cancelling immediately")
    }

    // MARK: - 2. No .cancelled before tick

    /// Before `tick()`, the state must NOT be `.cancelled`. The listener is
    /// still `.cancelling` — the port is held. Any attempt to rebind (start())
    /// must be rejected while `.cancelling` (RED: current code rebinds
    /// immediately because stop() returns before .cancelled).
    func testNoCancelledBeforeTick() throws {
        try fakeListener.startListening(onState: { _ in }, onPacket: { _, _ in })
        XCTAssertEqual(fakeListener.state, .ready)

        fakeListener.cancelListening()

        XCTAssertEqual(fakeListener.state, .cancelling,
                       "State must be .cancelling after cancelListening()")
        XCTAssertNotEqual(fakeListener.state, .cancelled,
                          "State must NOT be .cancelled before tick()")

        // While .cancelling, startListening must be rejected (port still held).
        XCTAssertThrowsError(try fakeListener.startListening(onState: { _ in }, onPacket: { _, _ in }),
                             "startListening must be rejected while .cancelling (NWError 48)")
    }

    // MARK: - 3. .cancelled after tick

    /// `tick()` advances `.cancelling` → `.cancelled`, simulating the async
    /// teardown completion of `NWListener`. After this, the port is released
    /// and `startListening` may succeed.
    func testCancelledAfterTick() throws {
        try fakeListener.startListening(onState: { _ in }, onPacket: { _, _ in })
        XCTAssertEqual(fakeListener.state, .ready)

        fakeListener.cancelListening()
        XCTAssertEqual(fakeListener.state, .cancelling)

        fakeListener.tick()

        XCTAssertEqual(fakeListener.state, .cancelled,
                       "tick() must transition .cancelling → .cancelled")

        // After .cancelled, startListening must succeed (port released).
        XCTAssertNoThrow(try fakeListener.startListening(onState: { _ in }, onPacket: { _, _ in }),
                         "startListening must succeed after .cancelled (port released)")
    }

    // MARK: - 4. stop() blocks until .cancelled

    /// `ESP32DiscoveryListener.stop()` must not return until the underlying
    /// listener reaches `.cancelled`. This ensures the UDP port is fully
    /// released before the caller can rebind.
    ///
    /// RED (current code): `stop()` calls `cancelListening()` and returns
    /// immediately — the state is still `.cancelling`, not `.cancelled`.
    func testStopBlocksUntilCancelled() throws {
        listener = makeListener()
        try listener.start()
        XCTAssertEqual(fakeListener.state, .ready)

        // Run stop() on a background queue so we can observe whether it has
        // returned before tick().
        var stopCompleted = false
        let stopQueue = DispatchQueue(label: "test-stop-blocks")
        stopQueue.async {
            self.listener.stop()
            stopCompleted = true
        }

        // Give stop() a moment to start on the background queue.
        let briefWait = XCTestExpectation(description: "brief wait for stop to start")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            briefWait.fulfill()
        }
        wait(for: [briefWait], timeout: 2.0)

        // Before tick(), stop() must still be blocked (with the fix).
        // Without the fix, stop() has already returned.
        XCTAssertFalse(stopCompleted,
                       "stop() must block until .cancelled is reached; it returned prematurely")
        XCTAssertEqual(fakeListener.state, .cancelling,
                       "State must be .cancelling while stop() is blocked")

        // Tick to advance .cancelling → .cancelled, unblocking stop().
        fakeListener.tick()

        // Now stop() should complete.
        let stopExpectation = XCTestExpectation(description: "stop completed")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            if stopCompleted {
                stopExpectation.fulfill()
            }
        }
        wait(for: [stopExpectation], timeout: 2.0)

        XCTAssertTrue(stopCompleted, "stop() must complete after .cancelled is reached")
        XCTAssertEqual(fakeListener.state, .cancelled)
    }

    // MARK: - 5. Rapid stop + start — no port conflict

    /// After `stop()` returns, the state must be `.cancelled` (port released).
    /// An immediate `start()` must succeed without NWError 48.
    ///
    /// RED (current code): `stop()` returns while `.cancelling`, so `start()`
    /// is called while the port is still held → startListening throws.
    func testRapidStopStartNoPortConflict() throws {
        listener = makeListener()
        try listener.start()
        XCTAssertEqual(fakeListener.state, .ready)

        // stop() on background queue; tick to unblock (with the fix).
        let stopExpectation = XCTestExpectation(description: "stop completed")
        DispatchQueue.global().async {
            self.listener.stop()
            stopExpectation.fulfill()
        }

        // Give stop() a moment to call cancelListening() (→ .cancelling)
        // before ticking, so the tick advances the state machine correctly.
        let briefWait = XCTestExpectation(description: "brief wait for stop to start")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            briefWait.fulfill()
        }
        wait(for: [briefWait], timeout: 2.0)

        // Before tick, state must be .cancelling (stop is blocked).
        XCTAssertEqual(fakeListener.state, .cancelling,
                       "State must be .cancelling while stop() is blocked")

        // Tick to advance .cancelling → .cancelled, unblocking stop().
        fakeListener.tick()

        wait(for: [stopExpectation], timeout: 2.0)

        // After stop() returns, state must be .cancelled (port released).
        XCTAssertEqual(fakeListener.state, .cancelled,
                       "After stop() returns, state must be .cancelled (port released)")

        // Immediate restart must succeed without port conflict.
        XCTAssertNoThrow(try listener.start(),
                         "start() after stop() must not throw (no port conflict)")
        XCTAssertEqual(fakeListener.state, .ready)
    }
}
