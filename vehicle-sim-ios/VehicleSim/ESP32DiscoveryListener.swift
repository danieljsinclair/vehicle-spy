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

final class ESP32DiscoveryListener {
    private let verifier: DiscoveryVerifier
    private let onDiscovered: @Sendable (DiscoveredESP32) -> Void
    private let onError: @Sendable (ESP32DiscoveryListenerError) -> Void
    private let logger = Logger(subsystem: "com.axxiant.vehiclesim", category: "ESP32Discovery")
    private let queue: DispatchQueue

    private let lock = NSLock()
    private var listenerStorage: NWListener?
    private var isListeningStorage = false

    private var listener: NWListener? {
        get { lock.withLock { listenerStorage } }
        set { lock.withLock { listenerStorage = newValue } }
    }

    var isListening: Bool {
        lock.withLock { isListeningStorage }
    }

    init(
        publicKey: Curve25519.Signing.PublicKey? = nil,
        onDiscovered: @escaping @Sendable (DiscoveredESP32) -> Void,
        onError: @escaping @Sendable (ESP32DiscoveryListenerError) -> Void = { _ in /* no-op: default error handler does nothing when no handler is provided */ },
        queue: DispatchQueue = .global(qos: .userInitiated)
    ) {
        self.verifier = DiscoveryVerifier(publicKey: publicKey)
        self.onDiscovered = onDiscovered
        self.onError = onError
        self.queue = queue
    }

    func start() throws {
        let parameters = NWParameters.udp
        parameters.allowLocalEndpointReuse = true

        guard let port = NWEndpoint.Port(rawValue: DiscoveryConstants.broadcastPort) else {
            throw ESP32DiscoveryListenerError.invalidPublicKey
        }

        let newListener = try NWListener(using: parameters, on: port)

        newListener.newConnectionHandler = { [weak self] connection in
            guard let self else { return }
            self.handleConnection(connection)
        }

        newListener.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            switch state {
            case .ready:
                self.logger.info("Discovery listener ready on port \(DiscoveryConstants.broadcastPort)")
                self.lock.withLock { self.isListeningStorage = true }
            case .failed(let error):
                self.logger.error("Discovery listener failed: \(error.localizedDescription)")
                self.lock.withLock { self.isListeningStorage = false }
                self.onError(.listenerFailed(error))
            case .cancelled:
                self.lock.withLock { self.isListeningStorage = false }
            default:
                break
            }
        }

        newListener.start(queue: queue)
        self.listener = newListener
    }

    func stop() {
        listener?.cancel()
        listener = nil
        lock.withLock { isListeningStorage = false }
    }

    deinit {
        stop()
    }

    // MARK: - Private

    private func handleConnection(_ connection: NWConnection) {
        connection.start(queue: queue)

        connection.receiveMessage { [weak self] data, _, isComplete, error in
            guard let self else { return }

            if let error {
                self.logger.debug("UDP receive error: \(error.localizedDescription)")
                connection.cancel()
                return
            }

            if let data, !data.isEmpty {
                self.processPacket(data, remoteEndpoint: connection.endpoint)
            }

            if !isComplete {
                connection.cancel()
            }
        }
    }

    private func processPacket(_ data: Data, remoteEndpoint: NWEndpoint) {
        do {
            let packet = try DiscoveryPacket.parse(data)
            try verifier.verify(packet)

            let address = remoteEndpoint.hostAddressString

            let discovered = DiscoveredESP32(
                deviceId: packet.deviceId,
                address: address,
                port: DiscoveryConstants.broadcastPort,
                canPort: packet.canPort,
                timestamp: packet.timestamp,
                receivedAt: Date()
            )

            DispatchQueue.main.async { [weak self] in
                self?.onDiscovered(discovered)
            }
        } catch {
            self.logger.debug("Dropped untrusted/malformed discovery packet: \(error.localizedDescription)")
        }
    }
}

// MARK: - NWEndpoint host address

private extension NWEndpoint {
    var hostAddressString: String {
        switch self {
        case .hostPort(let host, _):
            switch host {
            case .ipv4(let addr):
                return addr.debugDescription
            case .ipv6(let addr):
                return addr.debugDescription
            case .name(let name, _):
                return name
            @unknown default:
                return "unknown"
            }
        default:
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
