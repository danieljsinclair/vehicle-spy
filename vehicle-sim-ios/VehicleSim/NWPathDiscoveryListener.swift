import Foundation
import Network
import os.log

// MARK: - NWPathDiscoveryListener

/// Default `DiscoveryListening` implementation wrapping `NWListener`.
///
/// This is a behavior-preserving extraction of the UDP discovery logic that
/// previously lived inline in `ESP32DiscoveryListener`. The state transitions,
/// packet handling, and error propagation are identical — only the call site
/// has changed (the coordinator now owns the listener via the
/// `DiscoveryListening` protocol).
final class NWPathDiscoveryListener: DiscoveryListening {
    private let queue: DispatchQueue
    private let logger = Logger(subsystem: "com.axxiant.vehiclesim", category: "NWPathDiscovery")

    private let lock = NSLock()
    private var listenerStorage: NWListener?
    private var stateStorage: DiscoveryListenerState = .setup

    private var onStateHandler: ((DiscoveryListenerState) -> Void)?
    private var onPacketHandler: ((Data, String) -> Void)?

    private var listener: NWListener? {
        get { lock.withLock { listenerStorage } }
        set { lock.withLock { listenerStorage = newValue } }
    }

    // MARK: - DiscoveryListening

    var state: DiscoveryListenerState {
        lock.withLock { stateStorage }
    }

    func startListening(
        onState: @escaping (DiscoveryListenerState) -> Void,
        onPacket: @escaping (Data, String) -> Void
    ) throws {
        onStateHandler = onState
        onPacketHandler = onPacket

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

        newListener.stateUpdateHandler = { [weak self] nwState in
            guard let self else { return }
            switch nwState {
            case .ready:
                self.logger.info("Discovery listener ready on port \(DiscoveryConstants.broadcastPort)")
                self.lock.withLock { self.stateStorage = .ready }
                onState(.ready)
            case .failed(let error):
                self.logger.error("Discovery listener failed: \(error.localizedDescription)")
                self.lock.withLock { self.stateStorage = .failed }
                onState(.failed)
            case .cancelled:
                self.lock.withLock { self.stateStorage = .cancelled }
                onState(.cancelled)
            default:
                break
            }
        }

        newListener.start(queue: queue)
        self.listener = newListener
    }

    func cancelListening() {
        lock.withLock { stateStorage = .cancelling }
        onStateHandler?(.cancelling)
        listener?.cancel()
        listener = nil
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
        let address = remoteEndpoint.hostAddressString
        onPacketHandler?(data, address)
    }

    // MARK: - Init

    init(queue: DispatchQueue = .global(qos: .userInitiated)) {
        self.queue = queue
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
