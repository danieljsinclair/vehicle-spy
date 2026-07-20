import Foundation
import XCTest
import Combine
@testable import VehicleSim

// MARK: - Mock VehicleSimWrapperProtocol

final class MockVehicleSimWrapper: NSObject, VehicleSimWrapperProtocol {
    // MARK: - Connection Control
    var startDemoCalled = false
    var startBLECalled = false
    var stopCalled = false
    var connectToDeviceCalled = false
    var connectToDeviceParams: (address: String, deviceName: String, vehicleType: String)?
    var connectToDeviceResult = true

    func startDemo() {
        startDemoCalled = true
    }

    func startBLE() {
        startBLECalled = true
    }

    func stop() {
        stopCalled = true
    }

    func connect(toDevice address: String, deviceName: String, vehicleType: String) -> Bool {
        connectToDeviceCalled = true
        connectToDeviceParams = (address, deviceName, vehicleType)
        return connectToDeviceResult
    }

    // MARK: - BLE Scanning
    var scanForDevicesCalled = false
    var scanForDevicesTimeout: TimeInterval?
    var scanForDevicesResult: [VehicleSimDevice] = []

    func scan(forDevices timeout: TimeInterval) -> [VehicleSimDevice] {
        scanForDevicesCalled = true
        scanForDevicesTimeout = timeout
        return scanForDevicesResult
    }

    // MARK: - Vehicle Options
    var getVehicleOptionsCalled = false
    var getVehicleOptionsResult: [NSDictionary] = []

    func getVehicleOptions() -> [NSDictionary] {
        getVehicleOptionsCalled = true
        return getVehicleOptionsResult
    }

    var switchVehicleTypeCalled = false
    var switchVehicleTypeParam: String?
    var switchVehicleTypeResult = true

    func switchVehicleType(_ vehicleType: String) -> Bool {
        switchVehicleTypeCalled = true
        switchVehicleTypeParam = vehicleType
        return switchVehicleTypeResult
    }

    func disconnect() {
        stop()
    }

    // MARK: - Signal Values
    var throttlePercentValue: NSNumber?
    var speedKmhValue: NSNumber?
    var accelerationGValue: NSNumber?
    var brakePercentValue: NSNumber?
    var motorRpmValue: NSNumber?
    var motorTorqueNmValue: NSNumber?
    var gearSelectorValue: String?
    var steeringAngleDegValue: NSNumber?

    var throttlePercent: NSNumber? { throttlePercentValue }
    var speedKmh: NSNumber? { speedKmhValue }
    var accelerationG: NSNumber? { accelerationGValue }
    var brakePercent: NSNumber? { brakePercentValue }
    var motorRpm: NSNumber? { motorRpmValue }
    var motorTorqueNm: NSNumber? { motorTorqueNmValue }
    var gearSelector: String? { gearSelectorValue }
    var steeringAngleDeg: NSNumber? { steeringAngleDegValue }

    // MARK: - State
    var connectionStateValue: ConnectionState = .disconnected
    var isBluetoothReadyValue = false
    var connectedDeviceNameValue: String?
    var connectedDeviceAddressValue: String?
    var detectionInfoValue = ""
    var isReceivingDataValue = false
    var bleNotificationCountValue = 0
    var lastRawHexValue = ""

    var connectionState: ConnectionState { connectionStateValue }
    var isBluetoothReady: Bool { isBluetoothReadyValue }
    var connectedDeviceName: String? { connectedDeviceNameValue }
    var connectedDeviceAddress: String? { connectedDeviceAddressValue }
    var detectionInfo: String { detectionInfoValue }
    var isReceivingData: Bool { isReceivingDataValue }
    var bleNotificationCount: Int { bleNotificationCountValue }
    var lastRawHex: String { lastRawHexValue }

    // MARK: - Reset
    func reset() {
        startDemoCalled = false
        startBLECalled = false
        stopCalled = false
        connectToDeviceCalled = false
        connectToDeviceParams = nil
        connectToDeviceResult = true
        scanForDevicesCalled = false
        scanForDevicesTimeout = nil
        scanForDevicesResult = []
        getVehicleOptionsCalled = false
        getVehicleOptionsResult = []
        switchVehicleTypeCalled = false
        switchVehicleTypeParam = nil
        switchVehicleTypeResult = true
        throttlePercentValue = nil
        speedKmhValue = nil
        accelerationGValue = nil
        brakePercentValue = nil
        motorRpmValue = nil
        motorTorqueNmValue = nil
        gearSelectorValue = nil
        steeringAngleDegValue = nil
        connectionStateValue = .disconnected
        isBluetoothReadyValue = false
        connectedDeviceNameValue = nil
        connectedDeviceAddressValue = nil
        detectionInfoValue = ""
        isReceivingDataValue = false
        bleNotificationCountValue = 0
        lastRawHexValue = ""
    }
}