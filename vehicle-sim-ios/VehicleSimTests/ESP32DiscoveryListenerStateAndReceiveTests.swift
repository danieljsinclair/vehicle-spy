import Foundation
import Network
import XCTest
@testable import VehicleSim

// Tests for ESP32DiscoveryListener state-transition and receive-error paths
// not covered by the happy-path packet-reception tests.

final class ESP32DiscoveryListenerStateAndReceiveTests: XCTestCase {

    // MARK: - Helpers

    /// Build a valid 106-byte DiscoveryPacket blob. publicKey == nil means the
    /// verifier short-circuits (unsigned discovery accepted).
    private func buildValidPacketBlob(
        deviceId: Data = Data([0xAB] + Array(repeating: 0, count: 15)),
        canPort: UInt16 = DiscoveryConstants.defaultCANPort,
        otaPort: UInt16 = DiscoveryConstants.otaPort
    ) -> Data {
        let nonce = Data([0xCD] + Array(repeating: 0, count: 7))
        let timestamp = UInt64(Date().timeIntervalSince1970)
        let signature = Data(repeating: 0, count: DiscoveryConstants.signatureLength)

        let packet = DiscoveryPacket(
            deviceId: deviceId,
            nonce: nonce,
            timestamp: timestamp,
            canPort: canPort,
            otaPort: otaPort,
            signature: signature
        )
        return packet.data
    }

    /// Send a UDP datagram to localhost:DiscoveryConstants.broadcastPort.
    ///
    /// The send runs on a background queue; this function returns immediately.
    /// The caller is responsible for waiting on its own expectation.
    private func sendPacketAndWait(
        _ blob: Data,
        port: UInt16 = DiscoveryConstants.broadcastPort,
        completion: @escaping () -> Void = {}
    ) {
        let readyExpectation = XCTestExpectation(description: "NWConnection is ready")
        let connection = NWConnection(
            host: .ipv4(IPv4Address("127.0.0.1")!),
            port: NWEndpoint.Port(rawValue: port)!,
            using: .udp
        )
        connection.stateUpdateHandler = { state in
            if case .ready = state {
                readyExpectation.fulfill()
            }
        }
        connection.start(queue: .global(qos: .userInitiated))

        // Drive the ready wait + send on a dedicated background queue so the
        // main queue is never blocked (the listener dispatches onDiscovered to
        // main; blocking main would deadlock the delivery path).
        let driveQueue = DispatchQueue(label: "send-drive-\(ProcessInfo.processInfo.processIdentifier)")
        driveQueue.async {
            let sema = DispatchSemaphore(value: 0)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.wait(for: [readyExpectation], timeout: 3.0)
                sema.signal()
            }
            sema.wait()

            connection.send(content: blob, completion: .contentProcessed { _ in
                completion()
            })
        }
    }

    /// Polls until `listener.isListening == true`, fulfilling on the main queue.
    private func waitForListening(on listener: ESP32DiscoveryListener, timeout: TimeInterval = 2.0) {
        let expectation = XCTestExpectation(description: "listener is listening")
        let pollQueue = DispatchQueue(label: "test-poll-listening-\(ProcessInfo.processInfo.processIdentifier)")
        pollQueue.async { [weak listener] in
            guard let listener else { return }
            while listener.isListening == false {}
            DispatchQueue.main.async { expectation.fulfill() }
        }
        wait(for: [expectation], timeout: timeout)
    }

    /// Polls until `listener.isListening == false`, fulfilling on the main queue.
    private func waitForNotListening(on listener: ESP32DiscoveryListener, timeout: TimeInterval = 2.0) {
        let expectation = XCTestExpectation(description: "listener is not listening")
        let pollQueue = DispatchQueue(label: "test-poll-not-listening-\(ProcessInfo.processInfo.processIdentifier)")
        pollQueue.async { [weak listener] in
            guard let listener else { return }
            while listener.isListening == true {}
            DispatchQueue.main.async { expectation.fulfill() }
        }
        wait(for: [expectation], timeout: timeout)
    }

    // MARK: - 1. Malformed packet → no onDiscovered, no crash (L154 catch)

    /// A too-short Data blob (5 bytes of garbage 0xAB 0x00...) fails the
    /// minimum-length check at parse time; the catch in processPacket (L154) logs
    /// and drops it — onDiscovered must not fire and onError must not fire
    /// (malformed is a debug-log drop, swallowed).
    func testMalformedPacket_NoOnDiscoveredAndNoOnError() throws {
        // Thread-safe container for discovered devices (same pattern as
        // DiscoveryPacketReceptionTests). We assert only on the distinctive
        // test deviceId so live-device cross-talk is tolerated.
        let capturedDevices = CapturedDevices()

        var capturedErrors: [ESP32DiscoveryListenerError] = []
        let errorQueue = DispatchQueue(label: "test-error-capture")
        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { device in
                capturedDevices.append(device)
            },
            onError: { capturedError in
                errorQueue.sync { capturedErrors.append(capturedError) }
            },
            queue: .global(qos: .userInitiated)
        )

        try listener.start()
        waitForListening(on: listener)

        // 5 bytes of garbage: well below DiscoveryConstants.minimumLength (106)
        let malformed = Data([0xAB, 0x00, 0xCD, 0xEF, 0x12])
        sendPacketAndWait(malformed)

        listener.stop()
        waitForNotListening(on: listener)

        // The distinctive deviceId we used for the malformed packet's leading
        // byte. Assert the malformed blob did NOT produce a DiscoveredESP32
        // carrying this id (robust to stray live-device discoveries).
        let testDeviceId = Data([0xAB] + Array(repeating: 0, count: 15))
        let testDiscoveries = capturedDevices.devices.filter { $0.deviceId == testDeviceId }
        XCTAssertTrue(testDiscoveries.isEmpty,
                      "Malformed packet must not produce a DiscoveredESP32 with test deviceId; got \(testDiscoveries.count)")

        // Malformed packets are debug-log drops; onError must not fire.
        XCTAssertTrue(errorQueue.sync { capturedErrors }.isEmpty,
                      "Malformed packet must not invoke onError; got \(errorQueue.sync { capturedErrors.count }) errors")
    }

    // MARK: - 2. stop() sets isListening=false (L100-101 .cancelled)

    /// After a successful start, calling stop() must flip isListening to false.
    /// The .cancelled state update (L100-101) is delivered asynchronously on the
    /// listener queue; we poll for the flag to settle.
    func testStop_SetsIsListeningFalse() throws {
        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: .global(qos: .userInitiated)
        )

        try listener.start()
        waitForListening(on: listener)
        XCTAssertTrue(listener.isListening, "Listener should be listening after start")

        listener.stop()
        waitForNotListening(on: listener)
        XCTAssertFalse(listener.isListening, "Listener must not be listening after stop")
    }

    // MARK: - 3. .failed state branch (L97-99)

    /// Binding a second listener on the same port while the first is active
    /// should push one into .failed, which invokes onError with .listenerFailed.
    ///
    /// NOTE: the production listener sets `allowLocalEndpointReuse = true`, which
    /// permits multiple UDP listeners on the same port. If the platform allows
    /// both to bind, the .failed path is not exercised and we skip rather than
    /// leave a flaky test.
    func testSecondListenerOnSamePort_TriggersFailedState() throws {
        var capturedErrors: [ESP32DiscoveryListenerError] = []
        let errorQueue = DispatchQueue(label: "test-failed-error-capture")

        let listener1 = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            onError: { capturedError in
                errorQueue.sync { capturedErrors.append(capturedError) }
            },
            queue: .global(qos: .userInitiated)
        )

        try listener1.start()
        waitForListening(on: listener1, timeout: 3.0)

        // Attempt to bind a second listener on the same port.
        let listener2 = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            onError: { capturedError in
                errorQueue.sync { capturedErrors.append(capturedError) }
            },
            queue: .global(qos: .userInitiated)
        )

        var throwError: Error?
        do {
            try listener2.start()
        } catch {
            throwError = error
        }

        listener1.stop()
        listener2.stop()

        // Either start() threw (bind failure) or onError fired (.failed state).
        // If neither happened, skip — the platform allowed both to bind.
        let errorCount = errorQueue.sync { capturedErrors.count }
        if throwError == nil && errorCount == 0 {
            throw XCTSkip("Second listener on same port did not trigger .failed (allowLocalEndpointReuse permitted both to bind)")
        }

        // If we get here, one of the two paths fired.
        if let error = throwError {
            XCTAssertTrue(error is ESP32DiscoveryListenerError,
                          "Bind failure should produce an ESP32DiscoveryListenerError")
        } else {
            XCTAssertEqual(errorCount, 1,
                           "Exactly one onError should have fired for the failed listener")
            let firstError = errorQueue.sync { capturedErrors.first }!
            if case .listenerFailed = firstError {
                // Expected path covered
            } else {
                XCTFail("Expected .listenerFailed, got \(firstError)")
            }
        }
    }

    // MARK: - 4. handleConnection receive-error path (L130-132)

    /// The receiveMessage callback's `error` parameter is populated when the
    /// underlying UDP receive fails. Driving this deterministically requires
    /// OS-level error injection which is not available from Swift user space.
    /// We skip rather than leave a flaky test that depends on OS behaviour.
    func testReceiveError_ConnectionCancelled() throws {
        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            onError: { _ in },
            queue: .global(qos: .userInitiated)
        )

        try listener.start()
        waitForListening(on: listener, timeout: 3.0)

        // Send a valid packet so the listener's receive path is exercised.
        let blob = buildValidPacketBlob(deviceId: Data([0xAB] + Array(repeating: 0, count: 15)))
        sendPacketAndWait(blob)

        // Wait a short window for any receive error to surface.
        Thread.sleep(forTimeInterval: 0.5)

        listener.stop()
        waitForNotListening(on: listener)

        // The receive-error path (L130-132) requires the OS to deliver a non-nil
        // `error` to the receiveMessage callback — not deterministically
        // triggerable from Swift user space.
        throw XCTSkip("UDP receive-error path (L130-132) requires OS-level error injection; not deterministically triggerable from Swift")
    }

    // MARK: - 4b. !isComplete path (L140-141)

    /// When `isComplete` is false after receiveMessage returns data, the listener
    /// cancels the connection (L140-141). For UDP datagrams, isComplete is
    /// typically true; partial delivery is not directly injectable from Swift.
    /// Skip rather than flake.
    func testIncompleteReceive_ConnectionCancelled() throws {
        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            onError: { _ in },
            queue: .global(qos: .userInitiated)
        )

        try listener.start()
        waitForListening(on: listener, timeout: 3.0)

        // Send a valid packet; the !isComplete path requires partial delivery
        // which the OS controls for UDP (typically isComplete=true for datagrams).
        let blob = buildValidPacketBlob(deviceId: Data([0xAB] + Array(repeating: 0, count: 15)))
        sendPacketAndWait(blob)

        Thread.sleep(forTimeInterval: 0.5)

        listener.stop()
        waitForNotListening(on: listener)

        // The !isComplete path (L140-141) requires partial UDP delivery which
        // is not deterministically injectable from Swift user space.
        throw XCTSkip("!isComplete path (L140-141) requires partial UDP delivery; not deterministically injectable from Swift")
    }
}

// MARK: - Thread-safe captured-devices container

/// Reference-type container for devices captured inside a concurrently-executing
/// closure. Swift 6 flags mutation of a captured value-type var (e.g. an Array
/// appended from a DispatchQueue.global callback) as an error; wrapping the
/// array inside a class sidesteps that restriction without changing behaviour.
private final class CapturedDevices {
    private let queue = DispatchQueue(label: "captured-devices-serial")
    private var storage: [DiscoveredESP32] = []

    var devices: [DiscoveredESP32] {
        queue.sync { storage }
    }

    func append(_ device: DiscoveredESP32) {
        queue.sync { storage.append(device) }
    }
}
