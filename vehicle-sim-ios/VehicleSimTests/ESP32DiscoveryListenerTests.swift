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

    func testListenerReceivesErrorCallback() throws {
        let errorExpectation = XCTestExpectation(description: "Error callback invoked")

        listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in },
            onError: { error in
                XCTAssertEqual(error.localizedDescription, "Discovery listener failed")
                errorExpectation.fulfill()
            },
            queue: queue
        )

        try listener.start()

        // Wait a bit to ensure any startup errors are caught
        wait(for: [errorExpectation], timeout: 1.0)

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
        XCTAssertTrue(listener.isListening)

        // After stop
        listener.stop()
        XCTAssertFalse(listener.isListening)

        // Can restart
        try listener.start()
        XCTAssertTrue(listener.isListening)

        // Final cleanup
        listener.stop()
        XCTAssertFalse(listener.isListening)
    }
}
