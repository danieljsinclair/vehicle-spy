import Foundation
import XCTest

@testable import VehicleSim

// MARK: - TCP Transport Tests

final class TCPTransportTests: XCTestCase {

    // MARK: - Connection Lifecycle

    func testWrapperInitiallyDisconnected() {
        let wrapper = VehicleSimWrapper()
        XCTAssertEqual(wrapper.connectionState, ConnectionStateDisconnected)
        XCTAssertNil(wrapper.connectedDeviceName)
        XCTAssertNil(wrapper.connectedDeviceAddress)
    }

    func testTCPConnectionToInvalidAddressFails() {
        let wrapper = VehicleSimWrapper()
        // An unreachable address should fail to connect
        let result = wrapper.connect(
            toDevice: "tcp:192.0.2.1:3333",
            deviceName: "Test ESP32",
            vehicleType: "tesla_model3"
        )
        // Should return NO because the TCP connection will time out or fail
        XCTAssertFalse(result, "Connection to unreachable address should fail")
    }

    func testTCPConnectionWithInvalidFormatFails() {
        let wrapper = VehicleSimWrapper()
        // Missing port should still be accepted (defaults to 3333)
        let result = wrapper.connect(
            toDevice: "tcp:192.0.2.1",
            deviceName: "Test ESP32",
            vehicleType: "tesla_model3"
        )
        // Should fail because the host is unreachable, not because of format
        XCTAssertFalse(result, "Connection to unreachable host should fail")
    }

    func testTCPConnectionWithUnknownVehicleTypeFails() {
        let wrapper = VehicleSimWrapper()
        let result = wrapper.connect(
            toDevice: "tcp:192.0.2.1:3333",
            deviceName: "Test ESP32",
            vehicleType: "nonexistent_vehicle_type"
        )
        XCTAssertFalse(result, "Connection with unknown vehicle type should fail")
    }

    // MARK: - Signal Values Before Connection

    func testSignalValuesAreNilWhenDisconnected() {
        let wrapper = VehicleSimWrapper()
        XCTAssertNil(wrapper.throttlePercent)
        XCTAssertNil(wrapper.speedKmh)
        XCTAssertNil(wrapper.accelerationG)
        XCTAssertNil(wrapper.brakePercent)
        XCTAssertNil(wrapper.motorRpm)
        XCTAssertNil(wrapper.motorTorqueNm)
        XCTAssertNil(wrapper.gearSelector)
        XCTAssertNil(wrapper.steeringAngleDeg)
    }

    // MARK: - Stop Clears State

    func testStopClearsConnectionState() {
        let wrapper = VehicleSimWrapper()
        wrapper.stop()
        XCTAssertEqual(wrapper.connectionState, ConnectionStateDisconnected)
        XCTAssertNil(wrapper.connectedDeviceName)
        XCTAssertNil(wrapper.connectedDeviceAddress)
    }

    // MARK: - Demo Mode Still Works

    func testDemoModeConnects() {
        let wrapper = VehicleSimWrapper()
        wrapper.startDemo()
        XCTAssertEqual(wrapper.connectionState, ConnectionStateConnected)
        XCTAssertEqual(wrapper.connectedDeviceName, "Demo")
    }

    func testStopAfterDemoClearsState() {
        let wrapper = VehicleSimWrapper()
        wrapper.startDemo()
        XCTAssertEqual(wrapper.connectionState, ConnectionStateConnected)

        wrapper.stop()
        XCTAssertEqual(wrapper.connectionState, ConnectionStateDisconnected)
        XCTAssertNil(wrapper.throttlePercent)
    }

    // MARK: - Vehicle Options

    func testVehicleOptionsAvailable() {
        let wrapper = VehicleSimWrapper()
        let options = wrapper.getVehicleOptions()
        XCTAssertFalse(options.isEmpty, "Vehicle options should not be empty")
    }

    func testVehicleOptionsHaveRequiredKeys() {
        let wrapper = VehicleSimWrapper()
        let options = wrapper.getVehicleOptions()
        for option in options {
            XCTAssertNotNil(option["id"], "Each option should have an 'id' key")
            XCTAssertNotNil(option["displayName"], "Each option should have a 'displayName' key")
        }
    }

    // MARK: - TCP Address Parsing

    func testTCPAddressWithPort() {
        let wrapper = VehicleSimWrapper()
        // This tests the parsing indirectly: a well-formed tcp: address
        // with an unreachable host should fail at connection time, not at parse time
        let result = wrapper.connect(
            toDevice: "tcp:10.0.0.1:3333",
            deviceName: "Test",
            vehicleType: "tesla_model3"
        )
        // Should fail because host is unreachable, not because of bad format
        XCTAssertFalse(result)
    }

    func testTCPAddressWithDefaultPort() {
        let wrapper = VehicleSimWrapper()
        // No port specified -- should default to 3333
        let result = wrapper.connect(
            toDevice: "tcp:10.0.0.1",
            deviceName: "Test",
            vehicleType: "tesla_model3"
        )
        XCTAssertFalse(result, "Should fail at connection, not parsing")
    }

    // MARK: - Multiple Connections

    func testConnectingWhileConnectedStopsPrevious() {
        let wrapper = VehicleSimWrapper()
        wrapper.startDemo()
        XCTAssertEqual(wrapper.connectionState, ConnectionStateConnected)

        // Attempt a TCP connection (will fail because host is unreachable,
        // but should cleanly stop the demo first)
        _ = wrapper.connect(
            toDevice: "tcp:192.0.2.1:3333",
            deviceName: "Test",
            vehicleType: "tesla_model3"
        )
        // After a failed TCP connection attempt, the wrapper should be disconnected
        // (the previous demo connection was stopped)
        // Give a brief moment for the background thread to fail
        Thread.sleep(forTimeInterval: 0.5)
        XCTAssertEqual(wrapper.connectionState, ConnectionStateDisconnected)
    }

    // MARK: - Bluetooth State

    func testBluetoothNotRequiredForTCP() {
        let wrapper = VehicleSimWrapper()
        // isBluetoothReady should return true (BLEManager is created)
        // even though we're not using BLE
        XCTAssertTrue(wrapper.isBluetoothReady)
    }

    // MARK: - Detection Info

    func testDetectionInfoEmptyWhenDisconnected() {
        let wrapper = VehicleSimWrapper()
        XCTAssertEqual(wrapper.detectionInfo, "")
    }

    func testReceivingDataFalseWhenDisconnected() {
        let wrapper = VehicleSimWrapper()
        XCTAssertFalse(wrapper.isReceivingData)
    }

    func testBleNotificationCountZeroWhenDisconnected() {
        let wrapper = VehicleSimWrapper()
        XCTAssertEqual(wrapper.bleNotificationCount, 0)
    }

    func testLastRawHexEmptyWhenDisconnected() {
        let wrapper = VehicleSimWrapper()
        XCTAssertEqual(wrapper.lastRawHex, "")
    }
}
