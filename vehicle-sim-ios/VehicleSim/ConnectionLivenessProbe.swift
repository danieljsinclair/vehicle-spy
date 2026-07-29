import Foundation

// MARK: - ConnectionLivenessProbe

/// Protocol abstracting connection liveness checks so `VehicleViewModel` can
/// be tested with a deterministic fake instead of real wall-clock time.
///
/// The production implementation (`HeartbeatLiveness`) records the timestamp
/// of the last received heartbeat and considers the connection stale when no
/// heartbeat has arrived within `timeoutMs`.
protocol ConnectionLivenessProbe: AnyObject {
    /// Record that a heartbeat was received at `nowMs`.
    func recordHeartbeat(nowMs: UInt64)

    /// Returns `true` if the connection is stale (no heartbeat within the
    /// configured timeout as of `nowMs`).
    func isConnectionStale(nowMs: UInt64) -> Bool
}

// MARK: - HeartbeatLiveness

/// Default `ConnectionLivenessProbe` implementation.
///
/// Considers the connection stale when `nowMs - lastHeartbeatMs >= timeoutMs`.
final class HeartbeatLiveness: ConnectionLivenessProbe {
    private let timeoutMs: UInt64
    private var lastHeartbeatMs: UInt64 = 0
    private let lock = NSLock()

    init(timeoutMs: UInt64 = 1000) {
        self.timeoutMs = timeoutMs
    }

    func recordHeartbeat(nowMs: UInt64) {
        lock.withLock { lastHeartbeatMs = nowMs }
    }

    func isConnectionStale(nowMs: UInt64) -> Bool {
        lock.withLock {
            // A connection with no recorded heartbeat is considered stale
            // immediately (the device has never sent a heartbeat).
            return nowMs - lastHeartbeatMs >= timeoutMs
        }
    }
}

// MARK: - NSLock extension

private extension NSLocking {
    func withLock<T>(_ body: ()throws -> T) rethrows -> T {
        lock()
        defer { unlock() }
        return try body()
    }
}
