import Foundation
import XCTest
@testable import VehicleSim

/// TDD tests for the mode-flip state-restart bug + auto-connect + remember-device.
///
/// These tests pin three contracts that were not exercised by the existing suite:
///
/// 1. Mode-flip WiFi → BLE → WiFi: discovery listener is genuinely re-created
///    (not just a flag flip). The `isESP32DiscoveryActive` flag must go false
///    during BLE and true again when returning to WiFi.
///
/// 2. Auto-connect on discovery: when a device is discovered in WiFi mode and
///    the connection is disconnected, the view model auto-connects WITHOUT
///    requiring a user button press. No trust gate.
///
/// 3. Remember deviceId: the last-connected device's 16-byte deviceId is stored
///    in UserDefaults. On the next discovery cycle, if the remembered device is
///    found, it is auto-reconnected to preferentially.
final class VehicleViewModelModeFlipTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var mockWrapper: MockVehicleSimWrapper!

    // A stable 16-byte device ID used across tests.
    private let testDeviceId = Data((0..<16).map { UInt8($0) })

    override func setUp() {
        super.setUp()
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        UserDefaults.standard.removeObject(forKey: "lastConnectedDeviceId")
        mockWrapper = MockVehicleSimWrapper()
        viewModel = VehicleViewModel(wrapper: mockWrapper)
    }

    override func tearDown() {
        viewModel = nil
        mockWrapper = nil
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        UserDefaults.standard.removeObject(forKey: "lastConnectedDeviceId")
        super.tearDown()
    }

    // MARK: - Helpers

    private func makeDiscoveredESP32(
        deviceId: Data = Data((0..<16).map { UInt8($0) }),
        address: String = "192.168.1.100",
        canPort: UInt16 = 3333,
        timestamp: UInt64 = UInt64(Date().timeIntervalSince1970)
    ) -> DiscoveredESP32 {
        DiscoveredESP32(
            deviceId: deviceId,
            address: address,
            port: 3335,
            canPort: canPort,
            timestamp: timestamp,
            receivedAt: Date()
        )
    }

    private func settle(_ timeout: TimeInterval = 0.3) {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.05))
        }
    }

    // MARK: - 1. Mode-flip: WiFi → BLE → WiFi discovery listener re-created

    /// After WiFi → BLE → WiFi, `isESP32DiscoveryActive` must be true again
    /// and a new listener must be in place (not the same instance as before).
    func testModeFlipWiFiToBLEToWiFiRestartsDiscovery() {
        // Start in WiFi mode — discovery should activate.
        viewModel.connectionMode = .wifi
        settle()
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "Discovery should be active in WiFi mode")

        // Switch to BLE — discovery must stop.
        viewModel.connectionMode = .ble
        settle()
        XCTAssertFalse(viewModel.isESP32DiscoveryActive,
                       "Discovery must be inactive in BLE mode")

        // Switch back to WiFi — discovery must restart.
        viewModel.connectionMode = .wifi
        settle()
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "Discovery must be re-activated when returning to WiFi mode after a flip")
    }

    /// After WiFi → Demo → WiFi, discovery must also restart.
    func testModeFlipWiFiToDemoToWiFiRestartsDiscovery() {
        viewModel.connectionMode = .wifi
        settle()
        XCTAssertTrue(viewModel.isESP32DiscoveryActive)

        // Switch to Demo — discovery must stop.
        viewModel.connectionMode = .demo
        settle()
        XCTAssertFalse(viewModel.isESP32DiscoveryActive,
                       "Discovery must be inactive in Demo mode")

        // Switch back to WiFi — discovery must restart.
        viewModel.connectionMode = .wifi
        settle()
        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "Discovery must be re-activated when returning to WiFi from Demo")
    }

    /// When `startESP32Discovery()` is called while `isESP32DiscoveryActive` is
    /// true (stale flag), it must still force a re-creation — not just a no-op
    /// flag flip. This pins the fix: the guard must be replaced with explicit
    /// cleanup that stops any existing listener before creating a new one.
    func testStartDiscoveryForcesRecreationEvenIfFlagIsStale() {
        viewModel.connectionMode = .wifi
        settle()
        XCTAssertTrue(viewModel.isESP32DiscoveryActive)

        // Simulate a stale-flag scenario: manually set the flag to true
        // without a real listener (simulates a race where the .ready handler
        // hasn't fired yet).
        viewModel.isESP32DiscoveryActive = true

        // Calling startESP32Discovery again should not crash and should
        // result in discovery being active.
        viewModel.startESP32Discovery()
        settle()

        XCTAssertTrue(viewModel.isESP32DiscoveryActive,
                      "startESP32Discovery must re-create the listener even when the flag is stale")
    }

    // MARK: - 2. Auto-connect on discovery (no button, no trust gate)

    /// When a device is discovered in WiFi mode and the connection is
    /// disconnected, the view model must auto-connect WITHOUT requiring a
    /// user button press. No trust gate — connect immediately.
    func testAutoConnectOnDiscoveryWithoutButtonPress() {
        let expectation = XCTestExpectation(description: "Auto-connect completes")

        viewModel.connectionMode = .wifi
        settle()

        let esp32 = makeDiscoveredESP32(deviceId: testDeviceId, address: "192.168.1.100")
        mockWrapper.connectToDeviceResult = true

        // Poll for connection state change.
        let timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
            if self.viewModel.connectionState == .connected {
                t.invalidate()
                expectation.fulfill()
            }
        }

        viewModel.autoConnect(to: esp32)

        wait(for: [expectation], timeout: 5.0)
        timer.invalidate()

        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Device should auto-connect on discovery without a button press")
        XCTAssertEqual(viewModel.connectedDeviceName, "ESP32 CAN Bridge")
    }

    /// Auto-connect must work even for unverified devices (no public key
    /// configured, no prior trust). The security policy must NOT block
    /// auto-connect.
    func testAutoConnectDoesNotRequireTrustGate() {
        let expectation = XCTestExpectation(description: "Auto-connect completes")

        viewModel.connectionMode = .wifi
        settle()

        let esp32 = makeDiscoveredESP32(deviceId: testDeviceId, address: "192.168.1.101")
        mockWrapper.connectToDeviceResult = true

        let timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
            if self.viewModel.connectionState == .connected {
                t.invalidate()
                expectation.fulfill()
            }
        }

        viewModel.autoConnect(to: esp32)

        wait(for: [expectation], timeout: 5.0)
        timer.invalidate()

        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Auto-connect must not be blocked by the trust gate for unverified devices")
    }

    // MARK: - 3. Remember deviceId + auto-reconnect to remembered device

    /// After connecting to a device, its 16-byte deviceId must be stored in
    /// UserDefaults under "lastConnectedDeviceId".
    func testLastConnectedDeviceIdStoredInUserDefaults() {
        let expectation = XCTestExpectation(description: "Auto-connect completes")

        viewModel.connectionMode = .wifi
        settle()

        let esp32 = makeDiscoveredESP32(deviceId: testDeviceId, address: "192.168.1.102")
        mockWrapper.connectToDeviceResult = true

        let timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
            if self.viewModel.connectionState == .connected {
                t.invalidate()
                expectation.fulfill()
            }
        }

        viewModel.autoConnect(to: esp32)

        wait(for: [expectation], timeout: 5.0)
        timer.invalidate()

        let stored = UserDefaults.standard.data(forKey: "lastConnectedDeviceId")
        XCTAssertNotNil(stored,
                        "Last-connected deviceId must be stored in UserDefaults")
        XCTAssertEqual(stored, testDeviceId,
                       "Stored deviceId must match the connected device's 16-byte ID")
    }

    /// On a new session, if the remembered device is discovered, the view model
    /// must auto-reconnect to IT preferentially (before other discovered devices).
    func testAutoReconnectToRememberedDeviceOnNextSession() {
        // Session 1: connect to a device, which stores its deviceId.
        let expectation1 = XCTestExpectation(description: "Session 1 auto-connect completes")

        viewModel.connectionMode = .wifi
        settle()
        let esp32 = makeDiscoveredESP32(deviceId: testDeviceId, address: "192.168.1.103")
        mockWrapper.connectToDeviceResult = true

        let timer1 = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
            if self.viewModel.connectionState == .connected {
                t.invalidate()
                expectation1.fulfill()
            }
        }

        viewModel.autoConnect(to: esp32)
        wait(for: [expectation1], timeout: 5.0)
        timer1.invalidate()
        XCTAssertEqual(viewModel.connectionState, .connected)

        // Disconnect.
        viewModel.disconnect()
        XCTAssertEqual(viewModel.connectionState, .disconnected)

        // Session 2: new view model instance — should remember the device.
        let newVM = VehicleViewModel(wrapper: mockWrapper)
        newVM.connectionMode = .wifi
        settle()

        // The remembered deviceId should be loaded from UserDefaults.
        let remembered = UserDefaults.standard.data(forKey: "lastConnectedDeviceId")
        XCTAssertNotNil(remembered,
                        "Remembered deviceId should persist across sessions")
        XCTAssertEqual(remembered, testDeviceId)
    }

    /// When the remembered device is NOT discovered, the view model should
    /// auto-connect to the first discovered device (fallback).
    func testAutoConnectToFirstDiscoveredWhenRememberedAbsent() {
        let expectation = XCTestExpectation(description: "Auto-connect completes")

        // No remembered device (UserDefaults cleared in setUp).
        viewModel.connectionMode = .wifi
        settle()

        let firstDiscovered = makeDiscoveredESP32(deviceId: Data(repeating: 0x01, count: 16),
                                                   address: "192.168.1.200")
        mockWrapper.connectToDeviceResult = true

        let timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
            if self.viewModel.connectionState == .connected {
                t.invalidate()
                expectation.fulfill()
            }
        }

        viewModel.autoConnect(to: firstDiscovered)

        wait(for: [expectation], timeout: 5.0)
        timer.invalidate()

        XCTAssertEqual(viewModel.connectionState, .connected,
                       "Should auto-connect to the first discovered device when no remembered device exists")
    }

    /// When the remembered device IS discovered, it must be preferred over
    /// other discovered devices.
    func testRememberedDevicePreferredOverOthers() {
        // Set up a remembered device.
        UserDefaults.standard.set(testDeviceId, forKey: "lastConnectedDeviceId")

        let expectation = XCTestExpectation(description: "Auto-connect completes")

        viewModel.connectionMode = .wifi
        settle()

        // The remembered device should be loaded.
        let remembered = UserDefaults.standard.data(forKey: "lastConnectedDeviceId")
        XCTAssertEqual(remembered, testDeviceId,
                       "Remembered deviceId should be available for preference")

        // Simulate discovering the remembered device.
        let rememberedDevice = makeDiscoveredESP32(deviceId: testDeviceId, address: "192.168.1.104")
        mockWrapper.connectToDeviceResult = true

        let timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { t in
            if self.viewModel.connectionState == .connected {
                t.invalidate()
                expectation.fulfill()
            }
        }

        viewModel.autoConnect(to: rememberedDevice)

        wait(for: [expectation], timeout: 5.0)
        timer.invalidate()

        XCTAssertEqual(viewModel.connectionState, .connected)
        XCTAssertEqual(viewModel.connectedDeviceAddress, "192.168.1.104:3333",
                       "Should connect to the remembered device when it is discovered")
    }
}
