import Foundation
import Combine
import CryptoKit

enum ConnectionMode: String, CaseIterable, Codable {
    case ble = "BLE"
    case wifi = "WiFi"
    case demo = "Demo"
}

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
    @Published var connectionState: ConnectionState = .disconnected
    @Published var connectionStatus: String = "Disconnected"
    @Published var connectedDeviceName: String?
    @Published var connectedDeviceAddress: String?

    // MARK: - BLE Scanning
    @Published var isScanning: Bool = false
    @Published var isConnecting: Bool = false
    @Published var discoveredDevices: [DeviceEntry] = []

    // MARK: - Vehicle Selection
    @Published var selectedVehicle: String = ""

    // MARK: - ESP32 Discovery
    @Published var discoveredESP32s: [DiscoveredESP32] = []
    @Published var isESP32DiscoveryActive: Bool = false
    @Published var esp32DiscoveryError: String?
    @Published var autoConnectedESP32: DiscoveredESP32?

    // MARK: - WiFi Security
    @Published var wifiSecurityPolicy: WiFiSecurityPolicy = WiFiSecurityPolicy()
    @Published var wifiSecurityError: String?

    // MARK: - Connection Mode
    @Published var connectionMode: ConnectionMode {
        didSet {
            UserDefaults.standard.set(connectionMode.rawValue, forKey: "connectionMode")
            onConnectionModeChanged()
        }
    }

    var vehicleOptions: [(String, String)] {
        guard let wrapper = wrapper else { return [] }
        let options = wrapper.getVehicleOptions()
        if selectedVehicle.isEmpty && !options.isEmpty {
            selectedVehicle = options[0]["id"]!
        }
        return options.map { ($0["id"]!, $0["displayName"]!) }
    }

    // MARK: - Private
    private var wrapper: VehicleSimWrapper?
    private var updateTimer: Timer?
    private var discoveryListener: ESP32DiscoveryListener?

    struct DeviceEntry: Identifiable {
        let id = UUID()
        let name: String
        let address: String
        let rssi: Int
    }

    // MARK: - Lifecycle

    init() {
        let savedMode = UserDefaults.standard.string(forKey: "connectionMode") ?? ""
        self.connectionMode = ConnectionMode(rawValue: savedMode) ?? .ble
        wrapper = VehicleSimWrapper()
    }

    deinit {
        stopUpdates()
        wrapper?.stop()
        stopESP32Discovery()
    }

    // MARK: - Connection Mode

    private func onConnectionModeChanged() {
        // Stop any active connection when switching modes
        if connectionState != .disconnected {
            disconnect()
        }

        switch connectionMode {
        case .ble:
            stopESP32Discovery()
        case .wifi:
            startESP32Discovery()
        case .demo:
            stopESP32Discovery()
            startDemo()
        }
    }

    // MARK: - Demo Mode

    private func startDemo() {
        guard let wrapper = wrapper else { return }
        wrapper.startDemo()
        connectionState = .connected
        connectedDeviceName = "Demo"
        connectedDeviceAddress = "simulation"
        connectionStatus = "Demo"
        startPolling()
    }

    // MARK: - Connection Control

    func startBLE() {
        guard wrapper != nil else { return }
        wrapper?.startBLE()
        connectionState = .connecting
        connectionStatus = "Scanning"
    }

    func stop() {
        wrapper?.stop()
        connectionState = .disconnected
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
                    self.connectionState = .connected
                    self.connectedDeviceName = wrapper.connectedDeviceName
                    self.connectedDeviceAddress = wrapper.connectedDeviceAddress
                    self.discoveredDevices = []
                    self.connectionStatus = "Connected"
                    self.startPolling()
                } else {
                    self.connectionState = .disconnected
                    self.connectionStatus = "Connection Failed"
                }
            }
        }
    }

    func disconnect() {
        wrapper?.stop()
        connectionState = .disconnected
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
        guard let wrapper = wrapper, connectionState == .connected else { return }
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

    // MARK: - ESP32 Discovery (WiFi mode)

    func startESP32Discovery() {
        guard !isESP32DiscoveryActive else { return }

        discoveredESP32s = []
        esp32DiscoveryError = nil
        autoConnectedESP32 = nil
        wifiSecurityError = nil

        // Start listener with signature verification via the security policy
        let listener = ESP32DiscoveryListener(
            publicKey: wifiSecurityPolicy.publicKey,
            onDiscovered: { [weak self] discovered in
                guard let self else { return }
                if let idx = self.discoveredESP32s.firstIndex(where: { $0.address == discovered.address }) {
                    self.discoveredESP32s[idx] = discovered
                } else {
                    self.discoveredESP32s.append(discovered)
                }

                // Auto-connect on first verified discovery if in WiFi mode and not already connected
                if self.connectionMode == .wifi
                    && self.connectionState == .disconnected
                    && self.autoConnectedESP32 == nil
                {
                    // Check security policy before auto-connecting
                    do {
                        try self.wifiSecurityPolicy.allowConnection(discovered: discovered)
                        self.autoConnect(to: discovered)
                    } catch {
                        // Device not verified -- do not auto-connect
                        self.wifiSecurityError = error.localizedDescription
                    }
                }
            },
            onError: { [weak self] error in
                guard let self else { return }
                self.esp32DiscoveryError = error.localizedDescription
                self.isESP32DiscoveryActive = false
            }
        )

        do {
            try listener.start()
            discoveryListener = listener
            isESP32DiscoveryActive = true
        } catch {
            esp32DiscoveryError = error.localizedDescription
        }
    }

    func stopESP32Discovery() {
        discoveryListener?.stop()
        discoveryListener = nil
        isESP32DiscoveryActive = false
    }

    /// Manually connect to a discovered ESP32 at its CAN port.
    /// Checks the WiFi security policy first; refuses unverified devices by default.
    func connectToESP32(_ esp32: DiscoveredESP32) {
        guard wrapper != nil else { return }

        // Security policy check
        do {
            try wifiSecurityPolicy.allowConnection(discovered: esp32)
            wifiSecurityError = nil
        } catch {
            wifiSecurityError = error.localizedDescription
            connectionStatus = "Refused: Unverified Device"
            return
        }

        initiateESP32Connection(esp32)
    }

    /// Explicitly trust a discovered ESP32 device, bypassing signature verification.
    /// The user must confirm this action via the UI.
    func trustESP32(_ esp32: DiscoveredESP32) {
        wifiSecurityPolicy.markUserTrusted(deviceId: esp32.deviceId)
        // Remove from discovered list and re-add to update UI state
        if let idx = discoveredESP32s.firstIndex(where: { $0.address == esp32.address }) {
            discoveredESP32s[idx] = esp32
        }
    }

    private func autoConnect(to esp32: DiscoveredESP32) {
        autoConnectedESP32 = esp32
        initiateESP32Connection(esp32)
    }

    private func initiateESP32Connection(_ esp32: DiscoveredESP32) {
        guard let wrapper = wrapper else { return }
        let address = esp32.host
        let port = esp32.canPort
        let tcpTarget = "tcp:\(address):\(port)"

        connectionStatus = "Connecting to \(address):\(port)"
        connectionState = .connecting

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self, let wrapper = self.wrapper else { return }

            let success = wrapper.connect(toDevice: tcpTarget,
                                          deviceName: "ESP32 CAN Bridge",
                                          vehicleType: self.selectedVehicle)

            DispatchQueue.main.async {
                if success {
                    self.connectionState = .connected
                    self.connectedDeviceName = "ESP32 CAN Bridge"
                    self.connectedDeviceAddress = "\(address):\(port)"
                    self.connectionStatus = "Connected to ESP32"
                    self.autoConnectedESP32 = esp32
                    self.startPolling()
                } else {
                    self.connectionState = .disconnected
                    self.connectionStatus = "Connection Failed"
                }
            }
        }
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
}

// MARK: - Connection State Enum

enum ConnectionState {
    case disconnected
    case connecting
    case connected
}
