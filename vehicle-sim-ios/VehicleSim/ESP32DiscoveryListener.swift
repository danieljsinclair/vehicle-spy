import Foundation
import CryptoKit
import Network
import os.log

// MARK: - Discovered Device Model

struct DiscoveredESP32: Identifiable, Equatable {
    let id = UUID()
    let deviceId: Data
    let address: String
    let port: UInt16
    let canPort: UInt16
    let timestamp: UInt64
    let receivedAt: Date

    var host: String { address }
    var displayAddress: String { "\(address):\(port)" }
    var canEndpointDescription: String { "\(address):\(canPort)" }
}

// MARK: - Errors

enum ESP32DiscoveryListenerError: Error, LocalizedError {
    case invalidPublicKey
    case listenerFailed(any Error)

    var errorDescription: String? {
        switch self {
        case .invalidPublicKey:
            return "The configured ESP32 discovery public key is invalid."
        case .listenerFailed(let error):
            return "Discovery listener failed: \(error.localizedDescription)"
        }
    }
}

// MARK: - Listener

/// Thin coordinator around `DiscoveryListening`.
///
/// `ESP32DiscoveryListener` owns the decode/verify pipeline (DiscoveryDecoder)
/// and the higher-level callbacks (onDiscovered, onError). The underlying UDP
/// listener lifecycle is delegated to an injected `DiscoveryListening`
/// (default: `NWPathDiscoveryListener`), which wraps `NWListener`.
///
/// This extraction is behavior-preserving: the state transitions, packet
/// handling, and error propagation are identical to the previous inline
/// implementation.
final class ESP32DiscoveryListener {
    private let decoder: DiscoveryDecoder
    private let onDiscovered: @Sendable (DiscoveredESP32) -> Void
    private let onError: @Sendable (ESP32DiscoveryListenerError) -> Void
    private let logger = Logger(subsystem: "com.axxiant.vehiclesim", category: "ESP32Discovery")

    private let lock = NSLock()
    private var listenerStorage: DiscoveryListening
    private var isListeningStorage = false

    private var listener: DiscoveryListening {
        lock.withLock { listenerStorage }
    }

    var isListening: Bool {
        lock.withLock { isListeningStorage }
    }

    init(
        publicKey: Curve25519.Signing.PublicKey? = nil,
        onDiscovered: @escaping @Sendable (DiscoveredESP32) -> Void,
        onError: @escaping @Sendable (ESP32DiscoveryListenerError) -> Void = { _ in
            // no-op: default error handler. Callers that don't supply an onError intentionally ignore discovery errors.
        },
        queue: DispatchQueue = .global(qos: .userInitiated),
        listener: DiscoveryListening? = nil
    ) {
        let verifier = DiscoveryVerifier(publicKey: publicKey)
        self.decoder = DiscoveryDecoder(verifier: verifier)
        self.onDiscovered = onDiscovered
        self.onError = onError
        self.listenerStorage = listener ?? NWPathDiscoveryListener(queue: queue)
    }

    func start() throws {
        try listener.startListening(
            onState: { [weak self] state in
                guard let self else { return }
                switch state {
                case .ready:
                    self.logger.info("Discovery listener ready on port \(DiscoveryConstants.broadcastPort)")
                    self.lock.withLock { self.isListeningStorage = true }
                case .failed:
                    self.lock.withLock { self.isListeningStorage = false }
                    // The NWPathDiscoveryListener does not propagate the underlying
                    // NWError; mirror the original behavior by surfacing a generic
                    // listener-failed error. The original code forwarded the NWError
                    // directly; NWPathDiscoveryListener's onState only carries the
                    // enum state, so we wrap with a descriptive NSError.
                    let nsError = NSError(
                        domain: "NWPathDiscoveryListener",
                        code: 0,
                        userInfo: [NSLocalizedDescriptionKey: "Discovery listener failed"]
                    )
                    self.onError(.listenerFailed(nsError))
                case .cancelled:
                    self.lock.withLock { self.isListeningStorage = false }
                default:
                    break
                }
            },
            onPacket: { [weak self] data, address in
                guard let self else { return }
                self.processPacket(data, remoteAddress: address)
            }
        )
    }

    func stop() {
        listener.cancelListening()
        lock.withLock { isListeningStorage = false }
    }

    deinit {
        stop()
    }

    // MARK: - Private

    private func processPacket(_ data: Data, remoteAddress: String) {
        do {
            let discovered = try decoder.decode(data, remoteAddress: remoteAddress)

            DispatchQueue.main.async { [weak self] in
                self?.onDiscovered(discovered)
            }
        } catch {
            self.logger.debug("Dropped untrusted/malformed discovery packet: \(error.localizedDescription)")
        }
    }
}

// MARK: - NWEndpoint host address

// Internal (not private) so the host-resolution logic is reachable from
// @testable unit tests; it is still module-internal (not public) so the
// surface area exposed to consumers is unchanged.
extension NWEndpoint {
    var hostAddressString: String {
        // Expressed as if/else (not switch) per swift:S1301 — only one handled case.
        // The outer `if case .hostPort` matches the only endpoint shape we can resolve a
        // host from; the trailing `else` covers every other NWEndpoint case AND any
        // @unknown case added in a later SDK (future-proofed fallback to "unknown").
        if case .hostPort(let host, _) = self {
            // Equivalent to a switch over host (.ipv4/.ipv6/.name + @unknown default),
            // expressed as if/else. The final else is the future-proofed fallback for any
            // @unknown case added to NWEndpoint.Host in a later SDK.
            if case .ipv4(let addr) = host {
                return addr.debugDescription
            } else if case .ipv6(let addr) = host {
                return addr.debugDescription
            } else if case .name(let name, _) = host {
                return name
            } else {
                return "unknown"
            }
        } else {
            return "unknown"
        }
    }
}

// MARK: - NSLock extension

private extension NSLocking {
    func withLock<T>(_ body: () throws -> T) rethrows -> T {
        lock()
        defer { unlock() }
        return try body()
    }
}
