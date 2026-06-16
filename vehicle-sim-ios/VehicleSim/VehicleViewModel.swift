import Foundation
import Combine
import CryptoKit

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
    @Published var autoConnectEnabled: Bool = true
    @Published var autoConnectedESP32: DiscoveredESP32?

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
        wrapper = VehicleSimWrapper()
    }

    deinit {
        stopUpdates()
        wrapper?.stop()
        stopESP32Discovery()
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
                                          deviceName: device.name)

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
        wrapper?.disconnect()
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

    // MARK: - ESP32 Discovery

    /// Start listening for ESP32 discovery broadcasts.
    /// - Parameters:
    ///   - trustedDeviceId: The 16-byte device ID to trust.
    ///   - publicKey: The Ed25519 public key for signature verification.
    func startESP32Discovery(trustedDeviceId: Data, publicKey: Curve25519.Signing.PublicKey) {
        guard !isESP32DiscoveryActive else { return }

        discoveredESP32s = []
        esp32DiscoveryError = nil
        autoConnectedESP32 = nil

        let listener = ESP32DiscoveryListener(
            trustedDeviceId: trustedDeviceId,
            publicKey: publicKey,
            onDiscovered: { [weak self] discovered in
                guard let self else { return }
                // Deduplicate by address; update if re-discovered
                if let idx = self.discoveredESP32s.firstIndex(where: { $0.address == discovered.address }) {
                    self.discoveredESP32s[idx] = discovered
                } else {
                    self.discoveredESP32s.append(discovered)
                }

                // Auto-connect on first discovery if enabled and not already connected
                if self.autoConnectEnabled
                    && self.connectionState == .disconnected
                    && self.autoConnectedESP32 == nil
                {
                    self.autoConnect(to: discovered)
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

    /// Returns the `tcp:<host>:<port>` string for the given discovered ESP32,
    /// suitable for use with `vehicle-sim --connect`.
    func tcpConnectionString(for esp32: DiscoveredESP32) -> String {
        "tcp:\(esp32.host):\(esp32.canPort)"
    }

    /// Manually connect to a discovered ESP32 at its CAN port.
    func connectToESP32(_ esp32: DiscoveredESP32) {
        guard let wrapper = wrapper else { return }
        let address = esp32.host
        let port = esp32.canPort

        connectionStatus = "Connecting to \(address):\(port)"
        connectionState = .connecting

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self, let wrapper = self.wrapper else { return }

            let success = wrapper.connect(toTCPAddress: address, port: port)

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

    private func autoConnect(to esp32: DiscoveredESP32) {
        autoConnectedESP32 = esp32
        connectToESP32(esp32)
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
