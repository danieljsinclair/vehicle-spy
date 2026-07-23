import Foundation
import XCTest
import Combine
@testable import VehicleSim

// View-model connection + vehicle-selection contracts driven through the
// existing MockVehicleSimWrapper DI seam. These exercise the REAL
// VehicleViewModel async connect/switch paths; the mock only stands in for the
// Obj-C wrapper so the state-machine is deterministic.
//
// All status assertions check INTENT substrings (e.g. "connected", "failed"),
// never full message strings — the exact wording is not the contract.

final class VehicleViewModelConnectionTests: XCTestCase {

    private var viewModel: VehicleViewModel!
    private var mockWrapper: MockVehicleSimWrapper!
    private var cancellables: Set<AnyCancellable> = []

    override func setUp() {
        super.setUp()
        UserDefaults.standard.removeObject(forKey: "connectionMode")
        mockWrapper = MockVehicleSimWrapper()
        // Give the mock a vehicle option so selectedVehicle is non-empty when
        // connect paths need a vehicleType.
        mockWrapper.getVehicleOptionsResult = [
            ["id": "tesla_model3", "displayName": "Tesla Model 3"]
        ]
        viewModel = VehicleViewModel(wrapper: mockWrapper)
        // Accessing vehicleOptions has the side effect of defaulting
        // selectedVehicle to the first option id (mirrors how ContentView binds
        // it on first render). Do this once in setUp so tests that assert on
        // selectedVehicle see the populated default rather than "".
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

    // Drive the runloop until `predicate` is true or the timeout elapses. The
    // connect path hops to a global queue then back to main; this spins the main
    // runloop so those main-queue mutations land before we assert.
    private func waitFor(_ predicate: () -> Bool, timeout: TimeInterval = 1.0,
                          description: String = "condition") {
        let deadline = Date(timeIntervalSinceNow: timeout)
        while Date() < deadline {
            if predicate() { return }
            // Pump the main runloop so dispatched main-queue blocks execute.
            _ = RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.01))
        }
        XCTFail("Timed out waiting for \(description)")
    }

    // MARK: - #2 connectToDevice

    // Happy path: a successful wrapper connect transitions to .connected,
    // records device name/address, and clears the scanning list. Intent of the
    // status ("connected") is asserted, not the full string.
    func testConnectToDevice_Succeeds_TransitionsToConnected() {
        mockWrapper.connectToDeviceResult = true
        mockWrapper.connectedDeviceNameValue = "OBD2 Adapter"
        mockWrapper.connectedDeviceAddressValue = "AA:BB:CC:DD:EE:FF"

        let device = VehicleViewModel.DeviceEntry(name: "OBD2 Adapter", address: "AA:BB:CC:DD:EE:FF", rssi: -60)
        viewModel.connectToDevice(device)

        waitFor({ self.viewModel.connectionState == .connected },
                description: "connectionState == .connected")

        XCTAssertTrue(mockWrapper.connectToDeviceCalled, "wrapper.connect must be invoked")
        XCTAssertEqual(viewModel.connectionState, .connected)
        XCTAssertEqual(viewModel.connectedDeviceName, "OBD2 Adapter")
        XCTAssertEqual(viewModel.connectedDeviceAddress, "AA:BB:CC:DD:EE:FF")
        XCTAssertTrue(viewModel.connectionStatus.lowercased().contains("connected"),
                      "status should reflect connected intent; got \(viewModel.connectionStatus)")
        // On a successful connect, the discovered-devices list is cleared.
        XCTAssertTrue(viewModel.discoveredDevices.isEmpty)
    }

    // Failure path: when wrapper.connect returns false, the view model stays
    // disconnected and surfaces a failure intent in the status (no device name).
    func testConnectToDevice_Fails_StaysDisconnectedWithFailureStatus() {
        mockWrapper.connectToDeviceResult = false
        XCTAssertFalse(viewModel.isConnecting)

        let device = VehicleViewModel.DeviceEntry(name: "Bad Adapter", address: "00:00:00:00:00:00", rssi: -90)
        viewModel.connectToDevice(device)

        // isConnecting flips true synchronously before the async hop.
        XCTAssertTrue(viewModel.isConnecting, "isConnecting should be set immediately on connect")
        waitFor({ self.viewModel.isConnecting == false },
                description: "isConnecting cleared after connect resolves")

        XCTAssertEqual(viewModel.connectionState, .disconnected)
        XCTAssertNil(viewModel.connectedDeviceName)
        XCTAssertTrue(viewModel.connectionStatus.lowercased().contains("failed"),
                      "status should reflect failure intent; got \(viewModel.connectionStatus)")
    }

    // Connect forwards the device address, name, and the currently-selected
    // vehicle type to the wrapper — the three-parameter connect contract.
    func testConnectToDevice_ForwardsAddressNameAndVehicleType() {
        mockWrapper.connectToDeviceResult = true
        // selectedVehicle defaults to the first option id ("tesla_model3").
        XCTAssertEqual(viewModel.selectedVehicle, "tesla_model3")

        let device = VehicleViewModel.DeviceEntry(name: "Adapter X", address: "11:22:33:44:55:66", rssi: -42)
        viewModel.connectToDevice(device)

        waitFor({ self.mockWrapper.connectToDeviceCalled },
                description: "wrapper.connect called")
        let params = mockWrapper.connectToDeviceParams
        XCTAssertEqual(params?.address, "11:22:33:44:55:66")
        XCTAssertEqual(params?.deviceName, "Adapter X")
        XCTAssertEqual(params?.vehicleType, "tesla_model3")
    }

    // MARK: - #3 switchVehicleType

    // Refuses when disconnected: the guard (connectionState == .connected)
    // short-circuits, so the wrapper is never asked and selectedVehicle is
    // unchanged.
    func testSwitchVehicleType_RefusesWhenDisconnected() {
        XCTAssertEqual(viewModel.connectionState, .disconnected)
        let before = viewModel.selectedVehicle

        viewModel.switchVehicleType("generic")

        XCTAssertFalse(mockWrapper.switchVehicleTypeCalled,
                       "wrapper must not be called when disconnected")
        XCTAssertEqual(viewModel.selectedVehicle, before, "selection must not change when disconnected")
    }

    // On connected + wrapper success: forwards the switch and updates
    // selectedVehicle to the new type.
    func testSwitchVehicleType_OnConnectedSuccess_UpdatesSelection() {
        mockWrapper.switchVehicleTypeResult = true
        // Force the connected state (the guard only reads connectionState).
        viewModel.connectionState = .connected

        viewModel.switchVehicleType("generic")

        XCTAssertTrue(mockWrapper.switchVehicleTypeCalled)
        XCTAssertEqual(mockWrapper.switchVehicleTypeParam, "generic")
        XCTAssertEqual(viewModel.selectedVehicle, "generic")
    }

    // On connected + wrapper FAILURE: the switch is attempted but
    // selectedVehicle is NOT updated (the failure leaves the prior selection).
    func testSwitchVehicleType_OnConnectedFailure_KeepsPriorSelection() {
        mockWrapper.switchVehicleTypeResult = false
        viewModel.connectionState = .connected
        XCTAssertEqual(viewModel.selectedVehicle, "tesla_model3")

        viewModel.switchVehicleType("generic")

        XCTAssertTrue(mockWrapper.switchVehicleTypeCalled,
                      "wrapper must be attempted when connected")
        XCTAssertEqual(viewModel.selectedVehicle, "tesla_model3",
                       "selection must not change when the switch fails")
    }

    // MARK: - helpers
}
