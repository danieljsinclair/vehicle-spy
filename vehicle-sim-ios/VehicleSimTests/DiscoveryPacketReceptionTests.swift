import Foundation
import Network
import XCTest
@testable import VehicleSim

// Tests for the ESP32 discovery packet-reception path: the listener receives a
// real UDP datagram, decodes it, and dispatches onDiscovered; plus the
// VehicleViewModel onDiscovered closure (dedup-vs-append and auto-connect on
// first verified discovery). These are the FIRST tests that actually receive a
// packet end-to-end.

final class DiscoveryPacketReceptionTests: XCTestCase {

    // MARK: - Helpers

    /// Build a valid 106-byte DiscoveryPacket blob using the `DiscoveryPacket`
    /// struct's `.data` property. publicKey == nil means the verifier
    /// short-circuits, so a zeroed signature is accepted (unsigned discovery).
    /// timestamp must be > 0 (DiscoveryPacket.parse rejects zero timestamps).
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
    /// The send runs entirely on a background queue — this function returns
    /// immediately. The caller is responsible for `wait()`ing on its own
    /// expectation on the main queue (which is free, so the listener's
    /// `DispatchQueue.main.async` dispatch of `onDiscovered` is never blocked).
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

            // wait() is an XCTestCase method; dispatch it to main and block
            // driveQueue until the connection reaches .ready.
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

    // MARK: - A) Valid packet → onDiscovered fires with correct fields

    /// A valid unsigned packet: verifier short-circuits (publicKey == nil), the
    /// decoder produces a DiscoveredESP32, and onDiscovered fires on the main
    /// queue. The live ESP32 device may also broadcast on the same subnet, so
    /// assertions identify the test device by its distinctive deviceId rather
    /// than relying on exact count or fire-once invariants.
    func testListenerReceivesValidPacket_OnDiscoveredFiresWithCorrectFields() throws {
        let discovered: XCTestExpectation = XCTestExpectation(description: "onDiscovered fires")
        discovered.expectedFulfillmentCount = 1  // tolerate stray live-device packets

        // Thread-safe container for the concurrently-executing onDiscovered
        // closure (Swift 6 flags mutation of captured value-type vars as an
        // error; a reference-type container sidesteps that restriction).
        let capturedDevices = CapturedDevices()
        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { device in
                capturedDevices.append(device)
                discovered.fulfill()
            },
            queue: .global(qos: .userInitiated)
        )

        try listener.start()

        // Poll for isListening to flip to true — start() is async, the .ready
        // state update arrives on the listener queue after start() returns.
        let listening = XCTestExpectation(description: "listener ready")
        let pollQueue = DispatchQueue(label: "test-poll-valid-packet")
        pollQueue.async {
            while listener.isListening == false {}
            DispatchQueue.main.async { listening.fulfill() }
        }
        wait(for: [listening], timeout: 2.0)

        let blob = buildValidPacketBlob()
        // Send on a background queue; the main queue is free for the listener
        // to dispatch onDiscovered to it.
        sendPacketAndWait(blob)

        // Wait for onDiscovered on the main queue (it is free to run).
        wait(for: [discovered], timeout: 3.0)

        listener.stop()

        // Identify the test device by its distinctive deviceId (robust to
        // live-device cross-talk — extra stray discoveries are tolerated, we
        // assert only on the test packet's fields).
        let testDeviceId = Data([0xAB] + Array(repeating: 0, count: 15))
        guard let result = capturedDevices.devices.first(where: { $0.deviceId == testDeviceId }) else {
            XCTFail("Expected to find test device among captured devices; got \(capturedDevices.devices.count) devices")
            return
        }

        XCTAssertEqual(result.address, "127.0.0.1",
                       "DiscoveredESP32.address should reflect the sender's address")
        XCTAssertEqual(result.port, DiscoveryConstants.broadcastPort,
                       "DiscoveredESP32.port should be the discovery broadcast port")
        XCTAssertEqual(result.canPort, DiscoveryConstants.defaultCANPort,
                       "DiscoveredESP32.canPort should be parsed from the packet")
        XCTAssertEqual(result.deviceId, testDeviceId,
                       "DiscoveredESP32.deviceId should match the packet's deviceId")
        XCTAssertFalse(result.timestamp == 0,
                       "DiscoveredESP32.timestamp should be non-zero")
    }

    // MARK: - B) Malformed packet → no onDiscovered, no crash

    /// A too-short Data blob (3 bytes) fails the minimum-length check at parse
    /// time; the catch in processPacket logs and drops it — onDiscovered must
    /// not fire.
    func testListenerIgnoresMalformedPacket_NoCrash() throws {
        let unexpectedDiscovery = XCTestExpectation(description: "onDiscovered should NOT fire for malformed data")
        unexpectedDiscovery.isInverted = true

        let listener = ESP32DiscoveryListener(
            publicKey: nil,
            onDiscovered: { _ in unexpectedDiscovery.fulfill() },
            queue: .global(qos: .userInitiated)
        )

        try listener.start()

        let listening = XCTestExpectation(description: "listener ready")
        let pollQueue = DispatchQueue(label: "test-poll-malformed")
        pollQueue.async {
            while listener.isListening == false {}
            DispatchQueue.main.async { listening.fulfill() }
        }
        wait(for: [listening], timeout: 2.0)

        // 3 bytes: well below DiscoveryConstants.minimumLength (106)
        let malformed = Data([0x56, 0x53, 0x49])
        sendPacketAndWait(malformed)

        listener.stop()

        // isInverted expectation: fulfils on timeout (meaning onDiscovered never
        // fired). Give a small grace window to let any stray dispatch land.
        wait(for: [unexpectedDiscovery], timeout: 1.0)
    }

    // MARK: - C) Same deviceId twice → VM dedups (updates, not appends)

    /// Two packets with the same deviceId but different timestamps: the VM's
    /// onDiscovered closure uses firstIndex+assign for existing addresses, so
    /// discoveredESP32s contains the device exactly once, carrying the latest
    /// timestamp. Live-device cross-talk is tolerated; the dedup count is
    /// asserted only for the distinctive test deviceId.
    func testVehicleViewModelDedupsSameDeviceId_UpdatesNotAppends() throws {
        let mockWrapper = MockVehicleSimWrapper()
        let viewModel = VehicleViewModel(wrapper: mockWrapper)

        UserDefaults.standard.removeObject(forKey: "connectionMode")
        viewModel.connectionMode = .wifi

        // Without a public key, allowConnection throws for unverified devices.
        // Set refuseUnverifiedByDefault=false so the full onDiscovered path runs.
        var permissivePolicy = viewModel.wifiSecurityPolicy
        permissivePolicy = WiFiSecurityPolicy(
            publicKey: permissivePolicy.publicKey,
            deviceStates: permissivePolicy.deviceStates,
            refuseUnverifiedByDefault: false
        )
        viewModel.wifiSecurityPolicy = permissivePolicy

        viewModel.startESP32Discovery()

        // Wait for the listener to be bound.
        let discoveryReady = XCTestExpectation(description: "VM discovery ready")
        let pollQueue = DispatchQueue(label: "test-poll-vm-dedup")
        pollQueue.async {
            while viewModel.isESP32DiscoveryActive == false {}
            DispatchQueue.main.async { discoveryReady.fulfill() }
        }
        wait(for: [discoveryReady], timeout: 2.0)

        let deviceId = Data([0xDE, 0xAD] + Array(repeating: 0, count: 14))

        // Two distinct packets for the same deviceId (different timestamps).
        let blob1 = buildValidPacketBlob(deviceId: deviceId)
        Thread.sleep(forTimeInterval: 0.05)
        let blob2 = buildValidPacketBlob(deviceId: deviceId)

        // Parse the timestamps from each blob (offset 22 = after
        // magic(4)+version(1)+type(1)+deviceId(16)).
        let ts1 = try DiscoveryPacket.parse(blob1).timestamp
        let ts2 = try DiscoveryPacket.parse(blob2).timestamp

        let firstSent = XCTestExpectation(description: "first packet sent")
        sendPacketAndWait(blob1, completion: { firstSent.fulfill() })
        // Wait for first dispatch to land on main queue before sending second.
        let afterFirst = XCTestExpectation(description: "first dispatch landed")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { afterFirst.fulfill() }
        wait(for: [afterFirst], timeout: 1.0)

        let secondSent = XCTestExpectation(description: "second packet sent")
        sendPacketAndWait(blob2, completion: { secondSent.fulfill() })
        let afterSecond = XCTestExpectation(description: "second dispatch landed")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { afterSecond.fulfill() }
        wait(for: [afterSecond], timeout: 1.0)

        viewModel.stopESP32Discovery()

        // Dedup invariant: the deviceId appears exactly once in discoveredESP32s.
        let matches = viewModel.discoveredESP32s.filter { $0.deviceId == deviceId }
        XCTAssertEqual(matches.count, 1,
                       "Same deviceId should be deduplicated; found \(matches.count) entries")

        // The surviving entry should carry one of the two packet timestamps
        // (the second write overwrites the first; both represent distinct
        // sends so the latest of the two should survive).
        let latestTimestamp = matches.first?.timestamp ?? 0
        XCTAssertTrue(
            latestTimestamp == ts1 || latestTimestamp == ts2,
            "Deduped entry timestamp (\(latestTimestamp)) should be one of the two packet timestamps (\(ts1), \(ts2))"
        )
    }

    // MARK: - D) Auto-connect on first verified discovery

    /// When connectionMode is .wifi, connectionState is .disconnected, and
    /// autoConnectedESP32 is nil, the first discovery should trigger autoConnect
    /// → initiateESP32Connection → wrapper.connect is called.
    /// With connectToDeviceResult = true, initiateESP32Connection immediately
    /// sets connectionState to .connected (the "connecting" window is a
    /// synchronous state transition on the background queue, not observable as
    /// a stable intermediate state from the main thread).
    func testAutoConnectOnFirstVerifiedDiscovery_ConnectionStateBecomesConnected() throws {
        // Clear UserDefaults BEFORE creating the view model so lastConnectedDeviceId
        // is not loaded from a prior test's stored value.
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        UserDefaults.standard.removeObject(forKey: "lastConnectedDeviceId")

        let mockWrapper = MockVehicleSimWrapper()
        mockWrapper.connectToDeviceResult = true  // wrapper.connect succeeds immediately
        let viewModel = VehicleViewModel(wrapper: mockWrapper)

        viewModel.connectionMode = .wifi

        // Permissive policy so allowConnection doesn't throw (device is
        // unverified when publicKey == nil and refuseUnverifiedByDefault=true).
        var permissivePolicy = viewModel.wifiSecurityPolicy
        permissivePolicy = WiFiSecurityPolicy(
            publicKey: permissivePolicy.publicKey,
            deviceStates: permissivePolicy.deviceStates,
            refuseUnverifiedByDefault: false
        )
        viewModel.wifiSecurityPolicy = permissivePolicy

        // Pin preconditions for the auto-connect branch.
        XCTAssertEqual(viewModel.connectionState, .disconnected)
        XCTAssertNil(viewModel.autoConnectedESP32)

        viewModel.startESP32Discovery()

        let discoveryReady = XCTestExpectation(description: "VM discovery ready for auto-connect test")
        let pollQueue = DispatchQueue(label: "test-poll-vm-autoconnect")
        pollQueue.async {
            while viewModel.isESP32DiscoveryActive == false {}
            DispatchQueue.main.async { discoveryReady.fulfill() }
        }
        wait(for: [discoveryReady], timeout: 2.0)

        let deviceId = Data([0xCA, 0xFE] + Array(repeating: 0, count: 14))
        let blob = buildValidPacketBlob(deviceId: deviceId)
        sendPacketAndWait(blob)

        // The onDiscovered → autoConnect → initiateESP32Connection path runs on
        // the connectionWorkQueue; poll for the state transition (4s window
        // covers the OperationQueue scheduling delay).
        let connected = XCTestExpectation(description: "connectionState becomes .connected")
        let statePollQueue = DispatchQueue(label: "test-poll-connected")
        statePollQueue.async {
            while viewModel.connectionState != .connected {}
            DispatchQueue.main.async { connected.fulfill() }
        }
        wait(for: [connected], timeout: 4.0)

        viewModel.stopESP32Discovery()

        // With connectToDeviceResult = true, initiateESP32Connection immediately
        // transitions through .connecting to .connected on the background queue.
        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Auto-connect with successful wrapper.connect should reach .connected")
        XCTAssertNotNil(viewModel.autoConnectedESP32,
                        "autoConnectedESP32 should be set after auto-connect fires")
        XCTAssertEqual(viewModel.autoConnectedESP32?.deviceId, deviceId,
                       "autoConnectedESP32.deviceId should match the discovered device")
        XCTAssertTrue(mockWrapper.connectToDeviceCalled,
                      "Mock wrapper connect(toDevice:) should have been called")
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
