import Foundation
import Combine
import CryptoKit
import SwiftUI

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
    private var discoveryRetryTimer: Timer?
    private var cancellables = Set<AnyCancellable>()
    private let connectionWorkQueue: OperationQueue = {
        let q = OperationQueue()
        q.maxConcurrentOperationCount = 1
        q.qualityOfService = .userInitiated
        return q
    }()

    // Skip-list for IPs that failed authentication (wrong unit)
    // These IPs will be skipped for the remainder of the session
    // Cleared on mode switch, network change, or app restart
    private var authFailedIPs: Set<String> = []

    // Cap for authFailedIPs to prevent unbounded growth (L5). If the set grows
    // beyond this, it is cleared — these IPs are only a session-local skip-list,
    // so dropping stale entries is safe and bounded by the number of distinct
    // devices seen between clears.
    private let maxAuthFailedIPs = 50

    // L3: tag literals shared across the iOS layer. C++ has its own equivalents.
    private let clientTag = " [CLIENT]"
    private let esp32TagPrefix = "ESP32"

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

        // Subscribe to app lifecycle notifications
        NotificationCenter.default.publisher(for: .resumeDiscovery)
            .sink { [weak self] _ in
                self?.resumeDiscoveryIfNeeded()
            }
            .store(in: &cancellables)

        NotificationCenter.default.publisher(for: .pauseDiscovery)
            .sink { [weak self] _ in
                self?.pauseDiscoveryIfNeeded()
            }
            .store(in: &cancellables)
    }

    deinit {
        stopUpdates()
        wrapper?.stop()
        stopESP32Discovery()
    }

    // MARK: - Connection Mode

    private func onConnectionModeChanged() {
        // Clear auth-failed IP skip-list on mode switch (fresh slate)
        authFailedIPs.removeAll()

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
        connectionStatus = "Demo" + clientTag
        startPolling()
    }

    // MARK: - Connection Control

    func startBLE() {
        guard wrapper != nil else { return }
        wrapper?.startBLE()
        connectionState = .connecting
        connectionStatus = "Scanning" + clientTag
    }

    func stop() {
        wrapper?.stop()
        connectionState = .disconnected
        connectedDeviceName = nil
        connectedDeviceAddress = nil
        connectionStatus = "Disconnected" + clientTag
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
        connectionStatus = "Connecting" + clientTag

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
                    self.connectionStatus = "Connected" + self.clientTag
                    self.startPolling()
                } else {
                    self.connectionState = .disconnected
                    self.connectionStatus = "Connection Failed" + self.clientTag
                }
            }
        }
    }

    func disconnect() {
        wrapper?.stop()
        connectionState = .disconnected
        connectedDeviceName = nil
        connectedDeviceAddress = nil
        connectionStatus = "Disconnected" + clientTag
        stopUpdates()

        // Resume discovery after disconnect if in WiFi mode
        if connectionMode == .wifi {
            startESP32Discovery()
        }

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

                // Skip IPs that have previously failed authentication
                if self.authFailedIPs.contains(discovered.address) {
                    return
                }

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
                // Don't stop discovery on error - keep retrying
                // We'll restart the listener after a delay
                self.scheduleDiscoveryRetry()
            }
        )

        do {
            try listener.start()
            discoveryListener = listener
            isESP32DiscoveryActive = true
        } catch {
            esp32DiscoveryError = error.localizedDescription
            // Schedule retry even if initial start failed
            scheduleDiscoveryRetry()
        }
    }

    private func scheduleDiscoveryRetry() {
        // Retry ~once per second while app is active
        guard discoveryRetryTimer == nil else { return }

        discoveryRetryTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: false) { [weak self] _ in
            guard let self = self,
                  self.connectionMode == .wifi,
                  self.connectionState != .connected else {
                self?.discoveryRetryTimer = nil
                return
            }

            // Only retry if app is still active
            DispatchQueue.main.async {
                // Check if we should still be retrying
                if self.connectionMode == .wifi &&
                   self.connectionState != .connected &&
                   !self.isESP32DiscoveryActive {
                    self.discoveryRetryTimer = nil
                    self.startESP32Discovery()
                } else {
                    self.discoveryRetryTimer = nil
                }
            }
        }
    }

    func stopESP32Discovery() {
        discoveryListener?.stop()
        discoveryListener = nil
        discoveryRetryTimer?.invalidate()
        discoveryRetryTimer = nil
        isESP32DiscoveryActive = false
    }

    // MARK: - Discovery Lifecycle (App Background/Foreground)

    private func resumeDiscoveryIfNeeded() {
        guard connectionMode == .wifi else { return }

        // Clear auth-failed IP skip-list on app resume (network rejoin)
        authFailedIPs.removeAll()

        if !isESP32DiscoveryActive {
            startESP32Discovery()
        }
    }

    private func pauseDiscoveryIfNeeded() {
        // Only pause discovery if we're not connected
        guard connectionMode == .wifi,
              connectionState != .connected else { return }

        // Stop the listener but we'll restart when app becomes active
        stopESP32Discovery()
    }

    /// Manually connect to a discovered ESP32 at its CAN port.
    /// Checks the WiFi security policy first; refuses unverified devices by default.
    func connectToESP32(_ esp32: DiscoveredESP32) {
        guard wrapper != nil else { return }

        // Check if this IP has previously failed authentication
        if authFailedIPs.contains(esp32.address) {
            wifiSecurityError = "This device previously failed authentication"
            connectionStatus = "Skipping: Auth Failed Previously" + clientTag
            return
        }

        // Security policy check
        do {
            try wifiSecurityPolicy.allowConnection(discovered: esp32)
            wifiSecurityError = nil
        } catch {
            wifiSecurityError = error.localizedDescription
            connectionStatus = "Refused: Unverified Device" + clientTag
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

        // Pause discovery during connection attempt
        stopESP32Discovery()

        connectionStatus = "Connecting to \(address):\(port)" + clientTag
        connectionState = .connecting

        connectionWorkQueue.addOperation { [weak self] in
            guard let self = self else { return }

            // Refined retry policy: hunt for available devices
            // Auth failure = skip this IP and keep hunting, not "give up"
            var retryCount = 0
            let maxRetries = 60 // ~60 seconds total
            var targetDevice = esp32
            var foundNewCandidate = false

            while retryCount < maxRetries {
                guard let wrapper = self.wrapper else { break }

                // Check if we're still in WiFi mode
                DispatchQueue.main.async {
                    if self.connectionMode != .wifi {
                        // Mode changed, abort connection attempt
                        return
                    }
                }

                let currentAddress = targetDevice.host
                let currentPort = targetDevice.canPort
                let currentTarget = "tcp:\(currentAddress):\(currentPort)"

                let success = wrapper.connect(toDevice: currentTarget,
                                              deviceName: "ESP32 CAN Bridge",
                                              vehicleType: self.selectedVehicle)

                if success {
                    DispatchQueue.main.async {
                        self.connectionState = .connected
                        self.connectedDeviceName = "ESP32 CAN Bridge"
                        self.connectedDeviceAddress = "\(currentAddress):\(currentPort)"
                        let deviceIdHex = targetDevice.deviceId.map { String(format: "%02X", $0) }.joined()
                        if !deviceIdHex.isEmpty {
                            self.connectionStatus = "Connected to ESP32" + self.clientTag + " [" + self.esp32TagPrefix + ":" + deviceIdHex + "]"
                        } else {
                            self.connectionStatus = "Connected to ESP32" + self.clientTag
                        }
                        self.autoConnectedESP32 = targetDevice
                        self.startPolling()
                        // Discovery remains stopped when successfully connected
                    }
                    return
                }

                // Connection failed - assume auth failure (wrong unit)
                // Add this IP to skip-list and keep hunting for other devices
                DispatchQueue.main.async {
                    // Add failed IP to skip-list (bounded — see maxAuthFailedIPs)
                    if self.authFailedIPs.count >= self.maxAuthFailedIPs {
                        self.authFailedIPs.removeAll()
                    }
                    self.authFailedIPs.insert(currentAddress)

                    // Remove this device from discovered list
                    self.discoveredESP32s.removeAll { $0.address == currentAddress }

                    self.connectionStatus = "Auth Failed - Skipping \(currentAddress), hunting..." + self.clientTag
                }

                // Resume discovery to find other ESP32 devices on the network
                DispatchQueue.main.async {
                    if self.connectionMode == .wifi && self.connectionState != .connected {
                        self.startESP32Discovery()
                    }
                }

                // Wait for discovery to find new devices (with timeout)
                var waitedMs = 0
                let discoveryTimeoutMs = 5000 // Wait up to 5s for new discovery
                let checkIntervalMs = 100

                var shouldAbort = false

                while waitedMs < discoveryTimeoutMs && !foundNewCandidate && !shouldAbort {
                    Thread.sleep(forTimeInterval: Double(checkIntervalMs) / 1000.0)
                    waitedMs += checkIntervalMs

                    // Check if we found a new candidate.
                    // Synchronous read of main-affine state: connectionWorkQueue is a
                    // standalone OperationQueue that main never waits on, so .main.sync
                    // here cannot deadlock. Synchronous assignment ensures the loop
                    // observes the new candidate on this iteration, not ~100ms later.
                    DispatchQueue.main.sync {
                        if let newCandidate = self.discoveredESP32s.first(where: { device in
                            // Skip IPs in auth-failed list
                            !self.authFailedIPs.contains(device.address)
                        }) {
                            targetDevice = newCandidate
                            foundNewCandidate = true
                        }
                    }

                    // Exit if mode changed or connected elsewhere
                    DispatchQueue.main.sync {
                        if self.connectionMode != .wifi || self.connectionState == .connected {
                            shouldAbort = true
                        }
                    }
                }

                // If we found a new candidate, retry with it; otherwise, give up
                if shouldAbort {
                    // Mode changed or connected elsewhere — stop hunting explicitly
                    break
                }

                if foundNewCandidate {
                    retryCount = 0 // Reset retry count for new device
                    foundNewCandidate = false
                    // Pause discovery again for new connection attempt
                    DispatchQueue.main.async {
                        self.stopESP32Discovery()
                    }
                    continue
                }

                // No new device found, stop hunting
                break
            }

            // Hunting failed - no more candidates
            DispatchQueue.main.async {
                self.connectionState = .disconnected
                self.connectionStatus = "No more devices found - stopped hunting" + self.clientTag

                // Resume discovery for next attempt
                if self.connectionMode == .wifi {
                    self.startESP32Discovery()
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
