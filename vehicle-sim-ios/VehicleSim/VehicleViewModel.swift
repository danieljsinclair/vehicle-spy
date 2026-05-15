import Foundation
import Combine

class VehicleViewModel: ObservableObject {
    // MARK: - Signal Values
    @Published var throttlePercent: Double? = nil
    @Published var speed: Double? = nil
    @Published var acceleration: Double? = nil
    @Published var brakePercent: Double? = nil
    @Published var motorRpm: Double? = nil
    @Published var motorTorqueNm: Double? = nil
    @Published var gearSelector: String? = nil
    @Published var steeringAngleDeg: Double? = nil

    // MARK: - Connection State
    @Published var isConnected: Bool = false
    @Published var isDemoMode: Bool = false
    @Published var connectionStatus: String = "Disconnected"
    @Published var connectedDeviceName: String?
    @Published var connectedDeviceAddress: String?

    // MARK: - BLE Scanning
    @Published var isScanning: Bool = false
    @Published var isConnecting: Bool = false
    @Published var discoveredDevices: [DeviceEntry] = []

    // MARK: - Vehicle Selection
    @Published var selectedVehicle: String = "tesla_model3"

    let vehicleOptions = [
        ("tesla_model3", "Tesla Model 3/Y"),
        ("audi_mlb_evo", "Audi e-tron (MLB)"),
        ("generic", "Generic OBD2")
    ]

    // MARK: - Private
    private var wrapper: VehicleSimWrapper?
    private var updateTimer: Timer?

    struct DeviceEntry: Identifiable {
        let id = UUID()
        let name: String
        let address: String
        let rssi: Int
    }

    // MARK: - Lifecycle

    init() {
        wrapper = VehicleSimWrapper()
    }

    deinit {
        stopUpdates()
        wrapper?.disconnect()
    }

    // MARK: - Demo Mode

    func startDemo() {
        guard wrapper != nil else { return }
        wrapper?.startDemo()
        isDemoMode = true
        isConnected = false
        connectedDeviceName = nil
        connectedDeviceAddress = nil
        discoveredDevices = []
        connectionStatus = "Demo Mode"

        startPolling()
    }

    func stopDemo() {
        wrapper?.stopDemo()
        isDemoMode = false
        connectionStatus = "Disconnected"
        stopUpdates()
    }

    // MARK: - BLE Scanning

    func scanForDevices() {
        guard wrapper != nil else { return }
        isScanning = true
        discoveredDevices = []

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self, let wrapper = self.wrapper else { return }

            let devices = wrapper.scan(forDevices: 10)

            var entries: [DeviceEntry] = []
            for dev in devices {
                entries.append(DeviceEntry(
                    name: dev.name,
                    address: dev.address,
                    rssi: Int(dev.rssi)
                ))
            }

            DispatchQueue.main.async {
                self.discoveredDevices = entries
                self.isScanning = false
            }
        }
    }

    // MARK: - BLE Connection

    func connectToDevice(_ device: DeviceEntry) {
        guard wrapper != nil else { return }
        isConnecting = true
        connectionStatus = "Connecting"

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self, let wrapper = self.wrapper else { return }

            let success = wrapper.connect(toDevice: device.address,
                                          deviceName: device.name,
                                          vehicleType: self.selectedVehicle)

            DispatchQueue.main.async {
                self.isConnecting = false
                if success {
                    self.isConnected = true
                    self.isDemoMode = false
                    self.connectedDeviceName = wrapper.connectedDeviceName
                    self.connectedDeviceAddress = wrapper.connectedDeviceAddress
                    self.discoveredDevices = []
                    self.connectionStatus = "Connected"
                    self.startPolling()
                } else {
                    self.isConnected = false
                    self.connectionStatus = "Connection Failed"
                }
            }
        }
    }

    func disconnect() {
        wrapper?.disconnect()
        isConnected = false
        connectedDeviceName = nil
        connectedDeviceAddress = nil
        connectionStatus = "Disconnected"
        stopUpdates()

        throttlePercent = nil
        speed = nil
        acceleration = nil
        brakePercent = nil
        motorRpm = nil
        motorTorqueNm = nil
        gearSelector = nil
        steeringAngleDeg = nil
    }

    // MARK: - Vehicle Switching

    func switchVehicleType(_ newType: String) {
        guard let wrapper = wrapper, isConnected else { return }
        let success = wrapper.switchVehicleType(newType)
        if success {
            selectedVehicle = newType
        }
    }

    // MARK: - Detection

    var detectionInfo: String {
        return wrapper?.detectionInfo ?? ""
    }

    var isReceivingData: Bool {
        return wrapper?.isReceivingData ?? false
    }

    var bleNotificationCount: Int {
        return Int(wrapper?.bleNotificationCount ?? 0)
    }

    var lastRawHex: String {
        return wrapper?.lastRawHex ?? ""
    }

    // MARK: - Polling

    private func startPolling() {
        stopUpdates()
        updateTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            self?.updateTelemetry()
        }
    }

    private func stopUpdates() {
        updateTimer?.invalidate()
        updateTimer = nil
    }

    private func updateTelemetry() {
        guard let wrapper = wrapper else { return }

        if isDemoMode {
            wrapper.updateSimulator()
        }

        throttlePercent = wrapper.throttlePercent?.doubleValue
        speed = wrapper.speedKmh?.doubleValue
        acceleration = wrapper.accelerationG?.doubleValue
        brakePercent = wrapper.brakePercent?.doubleValue
        motorRpm = wrapper.motorRpm?.doubleValue
        motorTorqueNm = wrapper.motorTorqueNm?.doubleValue
        gearSelector = wrapper.gearSelector
        steeringAngleDeg = wrapper.steeringAngleDeg?.doubleValue
    }

    // MARK: - Helpers

    func vehicleDisplayName(_ id: String) -> String {
        vehicleOptions.first { $0.0 == id }?.1 ?? id
    }

    var isTeslaSelected: Bool { selectedVehicle == "tesla_model3" }
    var isAudiSelected: Bool { selectedVehicle == "audi_mlb_evo" }
}
