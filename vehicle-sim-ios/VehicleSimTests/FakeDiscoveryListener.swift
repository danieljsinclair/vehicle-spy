import Foundation
@testable import VehicleSim

// MARK: - FakeDiscoveryListener

/// Deterministic `DiscoveryListening` fake for tests.
///
/// State machine:
///   `.setup` → `.ready` (on startListening)
///   `.ready` → `.cancelling` (on cancelListening, immediate)
///   `.cancelling` → `.cancelled` (on tick)
///
/// `cancelListening()` sets `.cancelling` immediately and fires `onState(.cancelling)`.
/// `tick()` advances `.cancelling` → `.cancelled` and fires `onState(.cancelled)`.
/// This simulates the async teardown of `NWListener` without any real time or threads.
///
/// `injectPacket(data, addr)` delivers a packet via `onPacket` — only valid while
/// the listener is `.ready` (packets received while cancelling/cancelled are dropped,
/// matching NWListener semantics where cancelled listeners stop delivering).
final class FakeDiscoveryListener: DiscoveryListening {
    private let clock: FakeClock
    private let lock = NSLock()

    private var stateStorage: DiscoveryListenerState = .setup
    private var onStateHandler: ((DiscoveryListenerState) -> Void)?
    private var onPacketHandler: ((Data, String) -> Void)?

    init(clock: FakeClock) {
        self.clock = clock
    }

    // MARK: - DiscoveryListening

    var state: DiscoveryListenerState {
        lock.withLock { stateStorage }
    }

    func startListening(
        onState: @escaping (DiscoveryListenerState) -> Void,
        onPacket: @escaping (Data, String) -> Void
    ) throws {
        // Reject if the port is still held (simulates NWError 48:
        // "Address already in use"). A listener in .cancelling has not yet
        // released the UDP port; a listener in .ready is already bound.
        let canBind = lock.withLock { stateStorage != .cancelling && stateStorage != .ready }
        guard canBind else {
            throw ESP32DiscoveryListenerError.listenerFailed(
                NSError(domain: "FakeDiscoveryListener", code: 48,
                        userInfo: [NSLocalizedDescriptionKey: "Port still in use (NWError 48)"])
            )
        }

        onStateHandler = onState
        onPacketHandler = onPacket

        lock.withLock { stateStorage = .ready }
        onState(.ready)
    }

    func cancelListening() {
        lock.withLock {
            guard stateStorage == .ready else { return }
            stateStorage = .cancelling
        }
        onStateHandler?(.cancelling)
    }

    // MARK: - Test controls

    /// Advance the state machine: `.cancelling` → `.cancelled`.
    /// Fires `onState(.cancelled)`. No-op if already `.cancelled` or `.failed`.
    func tick() {
        lock.withLock {
            guard stateStorage == .cancelling else { return }
            stateStorage = .cancelled
        }
        onStateHandler?(.cancelled)
    }

    /// Deliver a packet as if received from `address` while the listener is `.ready`.
    /// Dropped if the listener is not in `.ready` state.
    func injectPacket(_ data: Data, from address: String) {
        let isReady = lock.withLock { stateStorage == .ready }
        guard isReady else { return }
        onPacketHandler?(data, address)
    }
}
