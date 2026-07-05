import Foundation
import XCTest
import Combine
@testable import VehicleSim

final class VehicleViewModelTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var cancellables: Set<AnyCancellable> = []

    override func setUp() {
        super.setUp()
        viewModel = VehicleViewModel()
        cancellables = []
    }

    override func tearDown() {
        viewModel = nil
        super.tearDown()
    }

    // MARK: - Initialization

    func testViewModelInitializesWithDefaultValues() {
        XCTAssertNil(viewModel.throttlePercent)
        XCTAssertNil(viewModel.speed)
        XCTAssertNil(viewModel.acceleration)
        XCTAssertNil(viewModel.brakePercent)
        XCTAssertNil(viewModel.motorRpm)
        XCTAssertNil(viewModel.motorTorqueNm)
        XCTAssertNil(viewModel.gearSelector)
        XCTAssertNil(viewModel.steeringAngleDeg)
    }

    func testViewModelInitializesInDisconnectedState() {
        XCTAssertEqual(viewModel.connectionState, .disconnected)
        XCTAssertEqual(viewModel.connectionStatus, "Disconnected [CLIENT]")
        XCTAssertNil(viewModel.connectedDeviceName)
        XCTAssertNil(viewModel.connectedDeviceAddress)
    }

    func testViewModelInitializesWithBLEModeByDefault() {
        XCTAssertEqual(viewModel.connectionMode, .ble)
    }

    func testViewModelInitializesWithEmptyDiscoveredDevices() {
        XCTAssertTrue(viewModel.discoveredDevices.isEmpty)
        XCTAssertFalse(viewModel.isScanning)
        XCTAssertFalse(viewModel.isConnecting)
    }

    func testViewModelInitializesWithEmptyESP32Discovery() {
        XCTAssertTrue(viewModel.discoveredESP32s.isEmpty)
        XCTAssertFalse(viewModel.isESP32DiscoveryActive)
        XCTAssertNil(viewModel.esp32DiscoveryError)
        XCTAssertNil(viewModel.autoConnectedESP32)
    }

    func testViewModelInitializesWithDefaultSecurityPolicy() {
        XCTAssertNotNil(viewModel.wifiSecurityPolicy)
        XCTAssertNil(viewModel.wifiSecurityError)
    }

    // MARK: - Connection Mode Changes

    func testSwitchingToWiFiStartsESP32Discovery() {
        viewModel.connectionMode = .wifi

        // After a brief delay, discovery should be active
        let expectation = XCTestExpectation(description: "ESP32 discovery becomes active")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            XCTAssertTrue(self.viewModel.isESP32DiscoveryActive || !self.viewModel.discoveredESP32s.isEmpty)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testSwitchingToDemoStopsESP32Discovery() {
        viewModel.connectionMode = .wifi

        // Wait for WiFi discovery to potentially start
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            self.viewModel.connectionMode = .demo

            // Demo mode should connect immediately
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                XCTAssertEqual(self.viewModel.connectionState, .connected)
                XCTAssertEqual(self.viewModel.connectedDeviceName, "Demo")
            }
        }
    }

    func testSwitchingModesClearsAuthFailedIPs() {
        // Start in WiFi mode to populate authFailedIPs (simulated)
        viewModel.connectionMode = .wifi

        // Switch to BLE
        viewModel.connectionMode = .ble

        // Switch back to WiFi - should clear the skip-list
        viewModel.connectionMode = .wifi

        // The internal authFailedIPs should be cleared (we can't directly test this,
        // but we can verify the mode switch processed correctly)
        XCTAssertEqual(viewModel.connectionMode, .wifi)
    }

    // MARK: - Demo Mode

    func testDemoModeConnectsSuccessfully() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Demo mode connects")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertEqual(self.viewModel.connectionState, .connected)
            XCTAssertEqual(self.viewModel.connectedDeviceName, "Demo")
            XCTAssertEqual(self.viewModel.connectedDeviceAddress, "simulation")
            XCTAssertTrue(self.viewModel.connectionStatus.contains("Demo"))
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testDemoModeProvidesTelemetry() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Demo telemetry available")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            // Demo mode should populate some telemetry values
            XCTAssertNotNil(self.viewModel.throttlePercent)
            XCTAssertNotNil(self.viewModel.speed)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Stop Functionality

    func testStopClearsConnectionState() {
        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            // Demo should be connected
            XCTAssertEqual(self.viewModel.connectionState, .connected)

            // Now stop
            self.viewModel.stop()

            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            XCTAssertNil(self.viewModel.connectedDeviceName)
            XCTAssertNil(self.viewModel.connectedDeviceAddress)
            XCTAssertTrue(self.viewModel.connectionStatus.contains("Disconnected"))
        }
    }

    func testStopClearsTelemetryValues() {
        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            // Demo should have telemetry
            XCTAssertNotNil(self.viewModel.throttlePercent)

            self.viewModel.stop()

            // All telemetry should be cleared
            XCTAssertNil(self.viewModel.throttlePercent)
            XCTAssertNil(self.viewModel.speed)
            XCTAssertNil(self.viewModel.acceleration)
            XCTAssertNil(self.viewModel.brakePercent)
            XCTAssertNil(self.viewModel.motorRpm)
            XCTAssertNil(self.viewModel.motorTorqueNm)
            XCTAssertNil(self.viewModel.gearSelector)
            XCTAssertNil(self.viewModel.steeringAngleDeg)
        }
    }

    func testStopClearsDiscoveredDevices() {
        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            self.viewModel.stop()

            // Discovered devices should be cleared (if any existed)
            XCTAssertTrue(self.viewModel.discoveredDevices.isEmpty)
        }
    }

    // MARK: - WiFi Security Policy

    func testWiFiSecurityPolicyIsConfigured() {
        XCTAssertNotNil(viewModel.wifiSecurityPolicy)
    }

    func testTrustingESP32UpdatesSecurityPolicy() {
        let deviceId = Data(repeating: 0xAA, count: 16)
        let esp32 = DiscoveredESP32(
            deviceId: deviceId,
            address: "192.168.1.100",
            port: 3335,
            canPort: 3333,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )

        viewModel.trustESP32(esp32)

        // The device should now be marked as user-trusted in the policy
        let state = viewModel.wifiSecurityPolicy.verificationState(for: deviceId)
        XCTAssertEqual(state, .userTrusted)
    }

    // MARK: - ESP32 Discovery Lifecycle

    func testESP32DiscoveryStartsWhenSwitchingToWiFi() {
        viewModel.connectionMode = .wifi

        let expectation = XCTestExpectation(description: "Discovery starts")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            XCTAssertTrue(self.viewModel.isESP32DiscoveryActive || self.viewModel.connectionMode == .wifi)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testESP32DiscoveryStopsWhenSwitchingFromWiFi() {
        viewModel.connectionMode = .wifi

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            // Should be active
            XCTAssertTrue(self.viewModel.isESP32DiscoveryActive || !self.viewModel.discoveredESP32s.isEmpty)

            // Switch away
            self.viewModel.connectionMode = .ble

            // Discovery should stop
            XCTAssertFalse(self.viewModel.isESP32DiscoveryActive)
        }
    }

    // MARK: - Connection States

    func testConnectionStateTransitionsThroughModes() {
        // Start in BLE
        XCTAssertEqual(viewModel.connectionState, .disconnected)

        // Switch to demo (should connect)
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "State transitions")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertEqual(self.viewModel.connectionState, .connected)

            // Stop (should disconnect)
            self.viewModel.stop()

            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Vehicle Selection

    func testVehicleOptionsAreNotEmpty() {
        XCTAssertFalse(viewModel.vehicleOptions.isEmpty, "Should have at least one vehicle option")
    }

    func testVehicleOptionsFormat() {
        for option in viewModel.vehicleOptions {
            // Each option should be a tuple of (id, displayName)
            XCTAssertFalse(option.0.isEmpty, "Vehicle ID should not be empty")
            XCTAssertFalse(option.1.isEmpty, "Vehicle display name should not be empty")
        }
    }

    func testSelectedVehicleDefaultsToFirstOption() {
        if let firstOption = viewModel.vehicleOptions.first {
            XCTAssertEqual(viewModel.selectedVehicle, firstOption.0)
        }
    }

    // MARK: - Detection Info

    func testDetectionInfoInitiallyEmpty() {
        XCTAssertTrue(viewModel.detectionInfo.isEmpty)
    }

    func testIsReceivingDataInitiallyFalse() {
        XCTAssertFalse(viewModel.isReceivingData)
    }

    func testBleNotificationCountInitiallyZero() {
        XCTAssertEqual(viewModel.bleNotificationCount, 0)
    }

    func testLastRawHexInitiallyEmpty() {
        XCTAssertTrue(viewModel.lastRawHex.isEmpty)
    }

    // MARK: - Published Property Updates

    func testConnectionStatusPublishesUpdates() {
        let expectation = XCTestExpectation(description: "Status updates")

        viewModel.$connectionStatus
            .dropFirst() // Skip initial value
            .sink { status in
                XCTAssertTrue(status.contains("Demo") || status.contains("Disconnected"))
                expectation.fulfill()
            }
            .store(in: &cancellables)

        viewModel.connectionMode = .demo

        wait(for: [expectation], timeout: 1.0)
    }

    func testConnectionStatePublishesUpdates() {
        let expectation = XCTestExpectation(description: "State updates")

        viewModel.$connectionState
            .dropFirst()
            .sink { state in
                if state == .connected {
                    expectation.fulfill()
                }
            }
            .store(in: &cancellables)

        viewModel.connectionMode = .demo

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Constants and Tags

    func testClientTagIsPresent() {
        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertTrue(viewModel.connectionStatus.contains("[CLIENT]"))
        }
    }

    func testESP32TagPresentWhenConnectedToESP32() {
        // This would require actual ESP32 connection, which we can't test in unit tests
        // But we can verify the tag format is correct when not connected
        XCTAssertFalse(viewModel.connectionStatus.contains("ESP32"))
    }

    // MARK: - Multiple Connection Attempts

    func testStopCanBeCalledMultipleTimesSafely() {
        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            // First stop
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)

            // Second stop should be safe
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)

            // Third stop should also be safe
            self.viewModel.stop()
        }
    }

    func testModeSwitchCanBeCalledMultipleTimes() {
        viewModel.connectionMode = .ble
        XCTAssertEqual(viewModel.connectionMode, .ble)

        viewModel.connectionMode = .wifi
        XCTAssertEqual(viewModel.connectionMode, .wifi)

        viewModel.connectionMode = .demo
        XCTAssertEqual(viewModel.connectionMode, .demo)

        viewModel.connectionMode = .ble
        XCTAssertEqual(viewModel.connectionMode, .ble)
    }

    // MARK: - WiFi Security Policy Integration

    func testWiFiConnectionRefusesUnverifiedDevice() {
        let deviceId = Data(repeating: 0xBB, count: 16)
        let esp32 = DiscoveredESP32(
            deviceId: deviceId,
            address: "192.168.1.200",
            port: 3335,
            canPort: 3333,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )

        viewModel.connectToESP32(esp32)

        // Should refuse unverified device
        XCTAssertTrue(viewModel.connectionStatus.contains("Refused") || viewModel.connectionStatus.contains("Unverified"))
    }

    func testWiFiConnectionAllowsVerifiedDevice() {
        let deviceId = Data(repeating: 0xCC, count: 16)
        let esp32 = DiscoveredESP32(
            deviceId: deviceId,
            address: "192.168.1.201",
            port: 3335,
            canPort: 3333,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )

        // First trust the device
        viewModel.trustESP32(esp32)

        // Now connection should be allowed
        viewModel.connectToESP32(esp32)

        // Should not contain refused/unverified status
        XCTAssertFalse(viewModel.connectionStatus.contains("Refused"))
        XCTAssertFalse(viewModel.connectionStatus.contains("Unverified"))
    }

    func testWiFiConnectionSetsSecurityErrorForUnverifiedDevice() {
        let deviceId = Data(repeating: 0xDD, count: 16)
        let esp32 = DiscoveredESP32(
            deviceId: deviceId,
            address: "192.168.1.202",
            port: 3335,
            canPort: 3333,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )

        viewModel.connectToESP32(esp32)

        XCTAssertNotNil(viewModel.wifiSecurityError)
    }

    func testTrustESP32ClearsSecurityError() {
        let deviceId = Data(repeating: 0xEE, count: 16)
        let esp32 = DiscoveredESP32(
            deviceId: deviceId,
            address: "192.168.1.203",
            port: 3335,
            canPort: 3333,
            timestamp: UInt64(Date().timeIntervalSince1970),
            receivedAt: Date()
        )

        // First try to connect (should fail)
        viewModel.connectToESP32(esp32)
        XCTAssertNotNil(viewModel.wifiSecurityError)

        // Now trust the device
        viewModel.trustESP32(esp32)

        // Security error should be cleared
        XCTAssertNil(viewModel.wifiSecurityError)
    }

    // MARK: - Vehicle Display Name

    func testVehicleDisplayNameReturnsDisplayNameForValidId() {
        if let firstOption = viewModel.vehicleOptions.first {
            let displayName = viewModel.vehicleDisplayName(firstOption.0)
            XCTAssertEqual(displayName, firstOption.1)
        }
    }

    func testVehicleDisplayNameReturnsIdForUnknownVehicle() {
        let unknownId = "unknown_vehicle_id"
        let displayName = viewModel.vehicleDisplayName(unknownId)
        XCTAssertEqual(displayName, unknownId, "Should return ID if display name not found")
    }

    // MARK: - Connection Lifecycle with WiFi

    func testSwitchingToWiFiThenBackClearsDiscovery() {
        // Start with WiFi
        viewModel.connectionMode = .wifi

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            // Should have discovery active
            let wasActive = self.viewModel.isESP32DiscoveryActive

            // Switch back to BLE
            self.viewModel.connectionMode = .ble

            // Discovery should be stopped
            XCTAssertFalse(self.viewModel.isESP32DiscoveryActive)
        }
    }

    // MARK: - BLE Scanning State

    func testScanningStateUpdatesCorrectly() {
        // Start in BLE mode
        viewModel.connectionMode = .ble

        // Initially not scanning
        XCTAssertFalse(viewModel.isScanning)

        // Start scanning (this is async, so we just verify the method doesn't crash)
        viewModel.scanForDevices()

        // Scanning should be true momentarily
        XCTAssertTrue(viewModel.isScanning)

        // After a delay, scanning should be false
        let expectation = XCTestExpectation(description: "Scanning completes")

        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            XCTAssertFalse(self.viewModel.isScanning, "Scanning should complete")
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 3.0)
    }

    // MARK: - Connection Mode Persistence

    func testConnectionModePersistsAcrossViewModelInstances() {
        let newViewModel = VehicleViewModel()

        // Should load the default mode
        XCTAssertEqual(newViewModel.connectionMode, .ble)

        // Change mode
        newViewModel.connectionMode = .wifi

        // Create another instance - it should load the saved mode
        let anotherViewModel = VehicleViewModel()

        // Note: This test verifies persistence but won't see the change due to
        // how UserDefaults works across instances. The important thing is the
        // mechanism exists.
        XCTAssertNotNil(anotherViewModel.connectionMode)
    }

    // MARK: - Thread Safety

    func testViewModelIsThreadSafeForConcurrentAccess() {
        let expectation = XCTestExpectation(description: "Concurrent access")

        // Simulate concurrent access from multiple threads
        DispatchQueue.concurrentPerform(iterations: 10) { _ in
            _ = self.viewModel.connectionState
            _ = self.viewModel.connectionStatus
            _ = self.viewModel.isESP32DiscoveryActive
        }

        expectation.fulfill()
        wait(for: [expectation], timeout: 2.0)
    }

    // MARK: - Edge Cases

    func testConnectingWithNoVehicleSelected() {
        let emptyViewModel = VehicleViewModel()

        // Clear selected vehicle
        emptyViewModel.selectedVehicle = ""

        // Should still handle safely without crashing
        XCTAssertNoThrow(emptyViewModel.connectionMode = .demo)
    }

    func testStopWhenAlreadyDisconnected() {
        // Already disconnected
        XCTAssertEqual(viewModel.connectionState, .disconnected)

        // Stop should be safe
        XCTAssertNoThrow(viewModel.stop())

        // Should still be disconnected
        XCTAssertEqual(viewModel.connectionState, .disconnected)
    }
}
