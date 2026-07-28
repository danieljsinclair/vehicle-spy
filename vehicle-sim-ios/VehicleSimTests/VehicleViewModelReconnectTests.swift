import Foundation
import XCTest
import Combine
@testable import VehicleSim

/// TDD tests for iOS persistent auto-reconnect on connection drop.
///
/// When the ESP32 reboots or WiFi comms are lost, the TCP transport
/// exhausts and `wrapper.isConnectionAlive` returns false. The ViewModel
/// must detect this, transition to `.disconnected`, and start a persistent
/// reconnect loop — no button press required. Being in WiFi mode is enough.
///
/// The reconnect loop:
///   1. Tries the remembered lastConnectedAddress directly (fast path).
///   2. Falls back to discovery-based auto-connect if no address or fast path fails.
///   3. Uses exponential backoff (1s, 2s, 4s, 8s, 16s, 30s cap).
///   4. Cancels on: successful reconnect (.connected), or leaving WiFi mode.
final class VehicleViewModelReconnectTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var mockWrapper: MockVehicleSimWrapper!
    private var cancellables: Set<AnyCancellable> = []

    // A stable 16-byte device ID used across tests.
    private let testDeviceId = Data((0..<16).map { UInt8($0) })

    override func setUp() {
        super.setUp()
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        UserDefaults.standard.removeObject(forKey: "lastConnectedDeviceId")
        mockWrapper = MockVehicleSimWrapper()
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"]
        ]
        viewModel = VehicleViewModel(wrapper: mockWrapper)
        _ = viewModel.vehicleOptions
        cancellables = []
    }

    override func tearDown() {
        viewModel?.stop()
        viewModel = nil
        mockWrapper = nil
        cancellables = []
        super.tearDown()
    }

    // MARK: - Helpers

    private func settle(_ timeout: TimeInterval = 0.3) {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
    }

    private func makeDiscoveredESP32(
        deviceId: Data = Data((0..<16).map { UInt8($0) }),
        address: String = "192.168.1.100",
        canPort: UInt16 = 3333
    ) -> DiscoveredESP32 {
        DiscoveredESP32(
            deviceId: deviceId,
            address: address,
            port: 3335,
            canPort: canPort,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )
    }

    /// Establishes a WiFi connection by calling autoConnect with a mock
    /// that returns success, then settles the runloop.
    private func establishWiFiConnection(
        address: String = "192.168.1.100",
        canPort: UInt16 = 3333
    ) {
        viewModel.connectionMode = .wifi
        settle()
        mockWrapper.connectToDeviceResult = true
        let esp32 = makeDiscoveredESP32(address: address, canPort: canPort)
        viewModel.autoConnect(to: esp32)
        // Wait for the connection to complete.
        let deadline = Date(timeIntervalSinceNow: 3.0)
        while viewModel.connectionState != .connected && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Precondition: WiFi connection should be established")
    }

    /// Waits for `mockWrapper.connectToDeviceCalled` to become true.
    /// Returns true if the call was made within the timeout.
    private func waitForConnectCall(timeout: TimeInterval = 3.0) -> Bool {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while !mockWrapper.connectToDeviceCalled && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        return mockWrapper.connectToDeviceCalled
    }

    /// Waits for `viewModel.connectionState` to become `.connected`.
    /// Returns true if connected within the timeout.
    private func waitForConnected(timeout: TimeInterval = 3.0) -> Bool {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while viewModel.connectionState != .connected && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        return viewModel.connectionState == .connected
    }

    // MARK: - 1. Connection drop detection

    /// When the wrapper's `isConnectionAlive` goes false while connected in
    /// WiFi mode, the ViewModel must detect the drop and start reconnecting.
    /// No button press — just being in WiFi mode is enough.
    func testConnectionDropStartsReconnectLoop() {
        establishWiFiConnection()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // Simulate connection drop: the wrapper reports the transport is dead.
        mockWrapper.isConnectionAliveValue = false

        // The polling timer (0.1s interval) should detect the drop.
        // We need to pump the runloop to let the timer fire.
        let deadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }

        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Connection state should transition to .disconnected after drop")
        XCTAssertTrue(viewModel.connectionStatus.lowercased().contains("reconnect"),
                      "Status should indicate reconnect intent; got \(viewModel.connectionStatus)")
    }

    /// The drop must NOT be detected when not in WiFi mode (BLE/demo).
    func testConnectionDropNotDetectedOutsideWiFiMode() {
        // Establish a BLE connection (connectionState = .connected).
        viewModel.connectionMode = .ble
        settle()
        mockWrapper.connectToDeviceResult = true
        let device = VehicleViewModel.DeviceEntry(name: "BLE Adapter", address: "AA:BB", rssi: -60)
        viewModel.connectToDevice(device)
        let deadline = Date(timeIntervalSinceNow: 2.0)
        while viewModel.connectionState != .connected && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .connected)

        // Simulate connection drop — but in BLE mode, isConnectionAlive
        // is not checked by the liveness poll.
        mockWrapper.isConnectionAliveValue = false
        settle(0.5)

        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Connection state should NOT change in BLE mode on drop")
    }

    // MARK: - 2. Reconnect loop: tries remembered address first

    /// After a drop, the reconnect loop should try the last connected
    /// address directly (fast path) before falling back to discovery.
    func testReconnectTriesLastConnectedAddressFirst() {
        establishWiFiConnection(address: "192.168.1.100", canPort: 3333)

        // Verify the last connected address was stored.
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = true

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Wait for the reconnect loop to schedule and fire.
        // The first backoff is 1s (2^0).
        XCTAssertTrue(waitForConnectCall(timeout: 3.0),
                      "Reconnect loop should attempt to connect after drop")
        XCTAssertNotNil(mockWrapper.connectToDeviceParams,
                        "Connect params should be set")
        // The reconnect should try tcp:<address>:<port>
        XCTAssertTrue(mockWrapper.connectToDeviceParams?.address.contains("tcp:") == true,
                      "Reconnect should use tcp: prefix; got \(mockWrapper.connectToDeviceParams?.address ?? "nil")")
        XCTAssertTrue(mockWrapper.connectToDeviceParams?.address.contains("192.168.1.100") == true,
                      "Reconnect should target the last connected address; got \(mockWrapper.connectToDeviceParams?.address ?? "nil")")
    }

    /// When the fast-path reconnect succeeds, the loop should cancel
    /// and the connection state should become .connected.
    func testReconnectSucceedsOnLastAddressCancelsLoop() {
        establishWiFiConnection()
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = true

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Wait for reconnect to fire and succeed.
        XCTAssertTrue(waitForConnected(timeout: 3.0),
                      "Should reconnect successfully to the last address")
        XCTAssertEqual(viewModel.connectionState, .connected)
        XCTAssertTrue(viewModel.connectionStatus.lowercased().contains("connected"),
                      "Status should reflect connected intent; got \(viewModel.connectionStatus)")
    }

    // MARK: - 3. Reconnect loop: falls back to discovery

    /// When in WiFi mode and disconnected (no connection to drop),
    /// discovery should be active.
    func testReconnectFallsBackToDiscoveryWhenNoLastAddress() {
        // Start in WiFi mode without establishing a connection first.
        viewModel.connectionMode = .wifi
        settle()
        XCTAssertEqual(viewModel.connectionState, .disconnected)

        // No lastConnectedAddress exists — the reconnect loop should
        // start discovery directly.
        //
        // Since attemptReconnect is private, we verify the behavior
        // indirectly: when in WiFi mode and disconnected, discovery
        // should be active (it was started by startESP32Discovery).
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "Discovery should be active when in WiFi mode and disconnected")
    }

    /// When the fast-path reconnect fails (wrong address), the loop should
    /// fall back to discovery.
    func testReconnectFallsBackToDiscoveryOnFastPathFailure() {
        establishWiFiConnection()
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = false // Fast path fails

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Wait for the reconnect attempt (1s backoff).
        XCTAssertTrue(waitForConnectCall(timeout: 3.0),
                      "Fast-path reconnect attempt should fire")

        // Give the main-thread dispatch time to execute startESP32Discovery.
        settle(1.0)

        // The fast path was attempted but failed. Discovery should now be active.
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "Discovery should be active after fast-path reconnect fails")
    }

    // MARK: - 4. Cancel on successful reconnect

    /// The reconnect loop must cancel when the connection succeeds.
    /// After a successful reconnect, no further reconnect attempts should
    /// be made.
    func testReconnectLoopCancelsOnSuccess() {
        establishWiFiConnection()
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = true

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected first.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Now wait for reconnect to succeed.
        XCTAssertTrue(waitForConnected(timeout: 3.0),
                      "Should reconnect successfully")

        // After successful reconnect, the mock's connect() set
        // isConnectionAliveValue = true. Reset only the call counter,
        // NOT isConnectionAliveValue (to avoid triggering another drop).
        mockWrapper.connectToDeviceCalled = false
        settle(2.0)

        XCTAssertFalse(mockWrapper.connectToDeviceCalled,
                       "No further reconnect attempts should be made after success")
    }

    // MARK: - 5. Cancel on leaving WiFi mode

    /// The reconnect loop must cancel when the user switches to a different
    /// mode (BLE or Demo).
    func testReconnectLoopCancelsOnModeSwitch() {
        establishWiFiConnection()
        mockWrapper.reset()

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false
        // Don't wait for reconnect to fire — switch mode immediately.
        viewModel.connectionMode = .ble

        XCTAssertEqual(viewModel.connectionMode, .ble)
        XCTAssertFalse(viewModel.isESP32DiscoveryActive,
                       "Discovery should be stopped in BLE mode")

        // Wait a bit to ensure no reconnect attempts happen.
        settle(1.0)
        XCTAssertFalse(mockWrapper.connectToDeviceCalled,
                       "No reconnect attempts should be made after leaving WiFi mode")
    }

    // MARK: - 6. Backoff between attempts

    /// The reconnect loop should use exponential backoff between attempts,
    /// not hammer the device.
    func testReconnectUsesBackoff() {
        establishWiFiConnection()
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = false // All attempts fail

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Wait for the first reconnect attempt (1s backoff).
        XCTAssertTrue(waitForConnectCall(timeout: 3.0),
                      "First reconnect attempt should fire")

        // Reset and wait for the second attempt. With 2s backoff (2^1), it should
        // take at least 2s. We check that the second call doesn't happen
        // immediately (within 0.5s).
        mockWrapper.connectToDeviceCalled = false
        settle(0.5)
        XCTAssertFalse(mockWrapper.connectToDeviceCalled,
                       "Second reconnect attempt should NOT fire immediately (backoff)")

        // Wait for the second attempt (should fire after ~2s backoff).
        XCTAssertTrue(waitForConnectCall(timeout: 3.0),
                      "Second reconnect attempt should fire after backoff")
    }

    // MARK: - 7. No button required

    /// The entire reconnect flow must work without any user interaction.
    /// The user just needs to be in WiFi mode.
    func testReconnectRequiresNoButtonPress() {
        establishWiFiConnection()
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = true

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected first.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Now wait for automatic reconnect.
        XCTAssertTrue(waitForConnected(timeout: 3.0),
                      "Should auto-reconnect without any button press")
        XCTAssertTrue(mockWrapper.connectToDeviceCalled,
                      "wrapper.connect should be called automatically")
    }

    // MARK: - 8. Discovery-based reconnect after drop

    /// After a drop, if the fast path fails, the discovery-based auto-connect
    /// should kick in when the ESP32 is rediscovered.
    func testDiscoveryBasedReconnectAfterDrop() {
        establishWiFiConnection()
        mockWrapper.reset()
        mockWrapper.connectToDeviceResult = true

        // Simulate drop.
        mockWrapper.isConnectionAliveValue = false

        // Wait for the drop to be detected.
        let dropDeadline = Date(timeIntervalSinceNow: 1.0)
        while viewModel.connectionState != .disconnected && Date() < dropDeadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
        XCTAssertEqual(viewModel.connectionState, .disconnected,
                       "Drop should be detected before reconnect")

        // Wait for the reconnect loop to start and fall back to discovery.
        // The fast path will fail (no real device), then discovery starts.
        let deadline = Date(timeIntervalSinceNow: 3.0)
        while !viewModel.isESP32DiscoveryActive && Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }

        // Now simulate the ESP32 being rediscovered.
        let esp32 = makeDiscoveredESP32(deviceId: testDeviceId, address: "192.168.1.100")
        viewModel.autoConnect(to: esp32)

        // Wait for connection.
        XCTAssertTrue(waitForConnected(timeout: 3.0),
                      "Should reconnect via discovery after drop")
    }
}
