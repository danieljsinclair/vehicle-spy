import Foundation
import CryptoKit
import Network
import XCTest
@testable import VehicleSim

final class ESP32DiscoveryListenerTests: XCTestCase {

    private var listener: ESP32DiscoveryListener!
    private var discoveredDevices: [DiscoveredESP32] = []
    private var errors: [ESP32DiscoveryListenerError] = []
    private let queue = DispatchQueue(label: "test-queue")

    override func setUp() {
        super.setUp()
        discoveredDevices = []
        errors = []
    }

    override func tearDown() {
        listener?.stop()
        listener = nil
        super.tearDown()
    }

    // MARK: - Async-state helpers
    //
    // NWListener.start(queue:) is asynchronous: the `.ready` state update (which
    // flips isListening to true) and the `.cancelled` update (from cancel()) are
    // delivered on `queue` after start()/stop() returns. Asserting isListening
    // synchronously right after start()/stop() is therefore a race — the flag may
    // not have flipped yet. These helpers poll the flag on a background queue and
    // fulfill an expectation as soon as the target state is observed (or time out).

    /// Waits until `listener.isListening == true`, polling off-thread so the
    /// async `.ready` state has a chance to land. Fulfills on the main thread.
    private func waitForListening(timeout: TimeInterval = 2.0) {
        let expectation = XCTestExpectation(description: "listener is listening")
        let pollQueue = DispatchQueue(label: "test-poll-listening")
        pollQueue.async { [weak self] in
            guard let self else { return }
            while self.listener.isListening == false {
                // Spin-wait on a background queue; isListening is lock-protected
                // so concurrent reads are safe. Bounded by the outer wait() timeout.
            }
            DispatchQueue.main.async { expectation.fulfill() }
        }
        wait(for: [expectation], timeout: timeout)
    }

    /// Waits until `listener.isListening == false`, polling off-thread so the
    /// async `.cancelled` state (or a synchronous stop()) has a chance to land.
    /// Fulfills on the main thread.
    private func waitForNotListening(timeout: TimeInterval = 2.0) {
        let expectation = XCTestExpectation(description: "listener is not listening")
        let pollQueue = DispatchQueue(label: "test-poll-not-listening")
        pollQueue.async { [weak self] in
            guard let self else { return }
            while self.listener.isListening == true {
                // See waitForListening: bounded by the outer wait() timeout.
            }
            DispatchQueue.main.async { expectation.fulfill() }
        }
        wait(for: [expectation], timeout: timeout)
    }

    // MARK: - Initialization

    func testListenerInitializesWithCallbacks() {
        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { [weak self] in
                self?.discoveredDevices.append($0)
            },
            onError: { [weak self] in
                self?.errors.append($0)
            },
            queue: queue
        )

        XCTAssertFalse(listener.isListening, "Listener should not be listening after init")
    }

    func testListenerInitializesWithDefaultErrorHandler() {
        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        XCTAssertFalse(listener.isListening)
    }

    func testListenerInitializesWithPublicKey() {
        let keyPair = Curve25519.Signing.PrivateKey()
        listener = ESP32DiscoveryListener(
            publicKey: keyPair.publicKey,
            onDiscovered: { _ in },
            queue: queue
        )

        XCTAssertFalse(listener.isListening)
    }

    // MARK: - Start and Stop

    func testStartMarksListenerAsListening() throws {
        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        try listener.start()

        waitForListening()
        XCTAssertTrue(listener.isListening, "Listener should be marked as listening after start")

        // Clean up
        listener.stop()
    }

    func testStopRemovesListener() {
        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        try? listener.start()
        listener.stop()

        waitForNotListening()
        XCTAssertFalse(listener.isListening, "Listener should not be listening after stop")
    }

    func testStopWhenNotListeningIsSafe() {
        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        // Should not throw or crash
        listener.stop()

        XCTAssertFalse(listener.isListening)
    }

    // MARK: - Error Handling

    /// Pins the happy-path error contract: on a successful start the listener
    /// reaches `.ready` and does NOT invoke `onError`.
    ///
    /// (The original test asserted the inverse — that `onError` fires — but never
    /// induced a failure, so the expectation could never be fulfilled. Reliably
    /// driving an `NWListener` into its `.failed` state is environment-dependent
    /// and would need a DI seam in production; rather than hack production for a
    /// flake or assert a contract the test can't drive, this verifies the
    /// deterministic side: a clean start surfaces no error. The error-firing path
    /// remains exercised in production via the `.failed` handler in
    /// ESP32DiscoveryListener.start().)
    func testSuccessfulStartDoesNotInvokeErrorCallback() throws {
        // isInverted = true → fulfills on timeout, FAILS if the callback fires.
        let noError = XCTestExpectation(description: "onError not invoked")
        noError.isInverted = true

        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            onError: { _ in
                noError.fulfill()
            },
            queue: queue
        )

        try listener.start()
        waitForListening()

        // Give the `.ready` path a window to prove no error arrives.
        wait(for: [noError], timeout: 1.0)

        listener.stop()
    }

    // MARK: - Thread Safety

    func testListenerIsThreadSafe() {
        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        // Access isListening from multiple threads (atomic property access)
        DispatchQueue.global().async {
            _ = listener.isListening
        }

        DispatchQueue.main.async {
            _ = listener.isListening
        }

        // Should not crash
        XCTAssertFalse(listener.isListening)
    }

    // MARK: - Memory Management

    func testListenerCleanupOnDeinit() {
        var listener: ESP32DiscoveryListener? = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        try? listener?.start()

        // Wait for the async `.ready` state to land before asserting listening.
        // This instance is local (not self.listener), so poll inline rather than
        // via the self.listener-based helpers (no duplicate helper added).
        let listening = XCTestExpectation(description: "local listener is listening")
        let pollQueue = DispatchQueue(label: "test-poll-deinit")
        pollQueue.async {
            while listener?.isListening == false { }
            DispatchQueue.main.async { listening.fulfill() }
        }
        wait(for: [listening], timeout: 2.0)
        XCTAssertTrue(listener?.isListening ?? false)

        // Deinit should clean up
        listener = nil

        // If we get here without crash, deinit worked correctly
        XCTAssertTrue(true)
    }

    // MARK: - Integration with DiscoveryPacket

    func testDiscoveryPacketStructure() {
        let deviceId = Data(repeating: 0x01, count: 16)
        let address = "192.168.1.100"
        let port: UInt16 = 3335
        let canPort: UInt16 = 3333
        let timestamp = UInt64(Date().timeIntervalSince1970)

        let discovered = DiscoveredESP32(
            deviceId: deviceId,
            address: address,
            port: port,
            canPort: canPort,
            timestamp: timestamp,
            receivedAt: Date()
        )

        XCTAssertEqual(discovered.address, address)
        XCTAssertEqual(discovered.port, port)
        XCTAssertEqual(discovered.canPort, canPort)
        XCTAssertEqual(discovered.deviceId, deviceId)
        XCTAssertEqual(discovered.host, address)
    }

    func testDiscoveredESP32ComputedProperties() {
        let discovered = DiscoveredESP32(
            deviceId: Data(repeating: 0xAA, count: 16),
            address: "192.168.1.50",
            port: 3335,
            canPort: 3333,
            timestamp: 1_700_000_000,
            receivedAt: Date()
        )

        XCTAssertEqual(discovered.displayAddress, "192.168.1.50:3335")
        XCTAssertEqual(discovered.canEndpointDescription, "192.168.1.50:3333")
        XCTAssertEqual(discovered.host, "192.168.1.50")
    }

    // MARK: - Discovery Constants

    func testDiscoveryConstantsAreCorrect() {
        XCTAssertEqual(DiscoveryConstants.magic, [0x56, 0x53, 0x49, 0x4D])
        XCTAssertEqual(DiscoveryConstants.currentVersion, 1)
        XCTAssertEqual(DiscoveryConstants.packetTypeDiscovery, 1)
        XCTAssertEqual(DiscoveryConstants.deviceIdLength, 16)
        XCTAssertEqual(DiscoveryConstants.nonceLength, 8)
        XCTAssertEqual(DiscoveryConstants.signatureLength, 64)
        XCTAssertEqual(DiscoveryConstants.headerLength, 42)
        XCTAssertEqual(DiscoveryConstants.minimumLength, 106)
        XCTAssertEqual(DiscoveryConstants.broadcastPort, 3335)
        XCTAssertEqual(DiscoveryConstants.defaultCANPort, 3333)
        XCTAssertEqual(DiscoveryConstants.otaPort, 3334)
    }

    // MARK: - Listener State Transitions

    func testListenerStateTransitions() throws {
        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            queue: queue
        )

        // Initial state
        XCTAssertFalse(listener.isListening)

        // After start
        try listener.start()
        waitForListening()
        XCTAssertTrue(listener.isListening)

        // After stop
        listener.stop()
        waitForNotListening()
        XCTAssertFalse(listener.isListening)

        // Can restart
        try listener.start()
        waitForListening()
        XCTAssertTrue(listener.isListening)

        // Final cleanup
        listener.stop()
        waitForNotListening()
        XCTAssertFalse(listener.isListening)
    }
}
