import Foundation

// MARK: - DiscoveryListenerState

/// State machine for a discovery listener.
///
/// Mirrors the NWListener state transitions that `ESP32DiscoveryListener`
/// previously handled inline: `setup` (pre-start), `ready` (listening),
/// `cancelling` (cancel() called, async teardown in flight), `cancelled`
/// (fully torn down), and `failed` (irrecoverable error).
enum DiscoveryListenerState {
    case setup
    case ready
    case cancelling
    case cancelled
    case failed
}

// MARK: - DiscoveryListening

/// Protocol abstracting the UDP discovery listener so `ESP32DiscoveryListener`
/// can be tested with a deterministic fake instead of real `NWListener`.
///
/// The contract mirrors the current `ESP32DiscoveryListener` behaviour:
/// - `startListening` binds the UDP port and begins delivering packets via the
///   `onState` and `onPacket` closures.
/// - `cancelListening` initiates async teardown (state → `.cancelling`, then
///   `.cancelled` once the underlying listener reports cancellation).
/// - `state` is the current state at any point.
protocol DiscoveryListening: AnyObject {
    /// Start listening for UDP discovery broadcasts.
    ///
    /// - Parameters:
    ///   - onState: Called whenever the listener's state changes.
    ///   - onPacket: Called for each received UDP datagram, with the raw
    ///     payload `Data` and the sender's resolved address string.
    /// - Throws: If the listener cannot bind the UDP port.
    func startListening(
        onState: @escaping (DiscoveryListenerState) -> Void,
        onPacket: @escaping (Data, String) -> Void
    ) throws

    /// Cancel the listener. Transitions to `.cancelling` immediately;
    /// the underlying NWListener reports `.cancelled` asynchronously.
    func cancelListening()

    /// The current listener state.
    var state: DiscoveryListenerState { get }
}
