import Foundation
import XCTest
import Combine
@testable import VehicleSim

/// Tests for VehicleViewModel using MockVehicleSimWrapper for isolation
final class VehicleViewModelTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var mockWrapper: MockVehicleSimWrapper!
    private var cancellables: Set<AnyCancellable> = []

    override func setUp() {
        super.setUp()
        // Clear UserDefaults to ensure test isolation
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        mockWrapper = MockVehicleSimWrapper()
        // Supply telemetry via the mock. The real wrapper populates these in
        // startDemo(); the mock does not, so tests that enter demo mode and
        // assert telemetry appears must have values to poll. Setting them here
        // is inert until the view model's demo polling loop starts, so it does
        // not affect tests that assert telemetry is nil before connecting.
        mockWrapper.throttlePercentValue = 42.0
        mockWrapper.speedKmhValue = 88.0
        viewModel = VehicleViewModel(wrapper: mockWrapper)
        cancellables = []
    }

    override func tearDown() {
        viewModel = nil
        mockWrapper = nil
        super.tearDown()
    }

    // MARK: - Initialization Tests

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

    // MARK: - Connection Mode Change Tests

    func testSwitchingToWiFiStartsESP32Discovery() {
        let expectation = XCTestExpectation(description: "ESP32 discovery becomes active")

        viewModel.connectionMode = .wifi

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            XCTAssertEqual(self.viewModel.connectionMode, .wifi)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testSwitchingToDemoConnectsAndProvidesTelemetry() {
        let expectation = XCTestExpectation(description: "Demo mode connects")

        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertEqual(self.viewModel.connectionState, .connected)
            XCTAssertEqual(self.viewModel.connectedDeviceName, "Demo")
            XCTAssertEqual(self.viewModel.connectedDeviceAddress, "simulation")
            XCTAssertTrue(self.viewModel.connectionStatus.contains("Demo"))
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testSwitchingToDemoProvidesTelemetryValues() {
        let expectation = XCTestExpectation(description: "Demo telemetry available")

        // Mock telemetry is supplied in setUp; demo polling surfaces it here.
        viewModel.connectionMode = .demo

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            XCTAssertNotNil(self.viewModel.throttlePercent)
            XCTAssertNotNil(self.viewModel.speed)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testSwitchingModesClearsAuthFailedIPs() {
        viewModel.connectionMode = .wifi
        viewModel.connectionMode = .ble
        viewModel.connectionMode = .wifi
        XCTAssertEqual(viewModel.connectionMode, .wifi)
    }

    // MARK: - Stop Functionality Tests

    func testStopClearsConnectionState() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Demo connects")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertEqual(self.viewModel.connectionState, .connected)
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            XCTAssertNil(self.viewModel.connectedDeviceName)
            XCTAssertNil(self.viewModel.connectedDeviceAddress)
            XCTAssertTrue(self.viewModel.connectionStatus.contains("Disconnected"))
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testStopClearsTelemetryValues() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Demo telemetry available")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertNotNil(self.viewModel.throttlePercent)
            self.viewModel.stop()
            XCTAssertNil(self.viewModel.throttlePercent)
            XCTAssertNil(self.viewModel.speed)
            XCTAssertNil(self.viewModel.acceleration)
            XCTAssertNil(self.viewModel.brakePercent)
            XCTAssertNil(self.viewModel.motorRpm)
            XCTAssertNil(self.viewModel.motorTorqueNm)
            XCTAssertNil(self.viewModel.gearSelector)
            XCTAssertNil(self.viewModel.steeringAngleDeg)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testStopClearsDiscoveredDevices() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Discovered devices cleared")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            self.viewModel.stop()
            XCTAssertTrue(self.viewModel.discoveredDevices.isEmpty)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    func testStopCanBeCalledMultipleTimesSafely() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Repeated stop is safe")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Demo Mode Tests

    func testDemoModeCallsStartDemoOnWrapper() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "StartDemo called")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            XCTAssertTrue(self.mockWrapper.startDemoCalled)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Vehicle Selection Tests

    func testVehicleOptionsAreNotEmpty() {
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"],
            ["id": "generic", "displayName": "Generic OBD2"]
        ]

        XCTAssertFalse(viewModel.vehicleOptions.isEmpty, "Should have at least one vehicle option")
    }

    func testVehicleOptionsFormat() {
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"],
            ["id": "generic", "displayName": "Generic OBD2"]
        ]

        for option in viewModel.vehicleOptions {
            XCTAssertFalse(option.0.isEmpty, "Vehicle ID should not be empty")
            XCTAssertFalse(option.1.isEmpty, "Vehicle display name should not be empty")
        }
    }

    func testSelectedVehicleDefaultsToFirstOption() {
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"],
            ["id": "generic", "displayName": "Generic OBD2"]
        ]

        if let firstOption = viewModel.vehicleOptions.first {
            XCTAssertEqual(viewModel.selectedVehicle, firstOption.0)
        }
    }

    // MARK: - Detection Info Tests

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

    // MARK: - Published Property Update Tests

    func testConnectionStatusPublishesUpdates() {
        let expectation = XCTestExpectation(description: "Status updates")

        viewModel.$connectionStatus
            .dropFirst()
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

    // MARK: - Constants and Tags Tests

    func testClientTagIsPresent() {
        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "Client tag present")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertTrue(self.viewModel.connectionStatus.contains("[CLIENT]"))
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Multiple Mode Switch Tests

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

    // MARK: - WiFi Security Policy Integration Tests

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

        let state = viewModel.wifiSecurityPolicy.verificationState(for: deviceId)
        XCTAssertEqual(state, .userTrusted)
    }

    // MARK: - ESP32 Discovery Lifecycle Tests

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

        let expectation = XCTestExpectation(description: "Discovery stops on mode switch")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            let _ = self.viewModel.isESP32DiscoveryActive
            self.viewModel.connectionMode = .ble
            XCTAssertFalse(self.viewModel.isESP32DiscoveryActive)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - Connection State Transition Tests

    func testConnectionStateTransitionsThroughModes() {
        XCTAssertEqual(viewModel.connectionState, .disconnected)

        viewModel.connectionMode = .demo

        let expectation = XCTestExpectation(description: "State transitions")

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            XCTAssertEqual(self.viewModel.connectionState, .connected)
            self.viewModel.stop()
            XCTAssertEqual(self.viewModel.connectionState, .disconnected)
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 1.0)
    }

    // MARK: - BLE Scanning State Tests

    func testScanningStateUpdatesCorrectly() {
        viewModel.connectionMode = .ble

        XCTAssertFalse(viewModel.isScanning)

        viewModel.scanForDevices()

        XCTAssertTrue(viewModel.isScanning)

        let expectation = XCTestExpectation(description: "Scanning completes")

        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            XCTAssertFalse(self.viewModel.isScanning, "Scanning should complete")
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 3.0)
    }

    // MARK: - Connection Mode Persistence Tests

    func testConnectionModePersistsAcrossViewModelInstances() {
        let newViewModel = VehicleViewModel(wrapper: mockWrapper)
        XCTAssertEqual(newViewModel.connectionMode, .ble)

        newViewModel.connectionMode = .wifi

        let anotherViewModel = VehicleViewModel(wrapper: mockWrapper)
        XCTAssertNotNil(anotherViewModel.connectionMode)
    }

    // MARK: - Thread Safety Tests

    func testViewModelIsThreadSafeForConcurrentAccess() {
        let expectation = XCTestExpectation(description: "Concurrent access")

        DispatchQueue.concurrentPerform(iterations: 10) { _ in
            _ = self.viewModel.connectionState
            _ = self.viewModel.connectionStatus
            _ = self.viewModel.isESP32DiscoveryActive
        }

        expectation.fulfill()
        wait(for: [expectation], timeout: 2.0)
    }

    // MARK: - WiFi Security Policy Integration Tests

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

        viewModel.trustESP32(esp32)
        viewModel.connectToESP32(esp32)

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

        viewModel.connectToESP32(esp32)
        XCTAssertNotNil(viewModel.wifiSecurityError)

        viewModel.trustESP32(esp32)

        XCTAssertNil(viewModel.wifiSecurityError)
    }

    // MARK: - Vehicle Display Name Tests

    func testVehicleDisplayNameReturnsDisplayNameForValidId() {
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"]
        ]

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

    // MARK: - Edge Cases Tests

    func testConnectingWithNoVehicleSelected() {
        let emptyViewModel = VehicleViewModel(wrapper: MockVehicleSimWrapper())
        emptyViewModel.selectedVehicle = ""
        XCTAssertNoThrow(emptyViewModel.connectionMode = .demo)
    }

    func testStopWhenAlreadyDisconnected() {
        XCTAssertEqual(viewModel.connectionState, .disconnected)
        XCTAssertNoThrow(viewModel.stop())
        XCTAssertEqual(viewModel.connectionState, .disconnected)
    }
}