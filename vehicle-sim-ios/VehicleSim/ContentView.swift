import SwiftUI

struct TelemetryCardView: View {
    let title: String
    let value: String
    let unit: String
    let color: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(.caption)
                .foregroundColor(.secondary)
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(value)
                    .font(.title2)
                    .fontWeight(.semibold)
                    .foregroundColor(color)
                Text(unit)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }
}

private struct ConnectingDotsView: View {
    @State private var dotCount = 0
    private let timer = Timer.publish(every: 0.4, on: .main, in: .common).autoconnect()

    var body: some View {
        Text(String(repeating: ".", count: dotCount))
            .foregroundColor(.orange)
            .onReceive(timer) { _ in
                dotCount = (dotCount % 3) + 1
            }
    }
}

private struct PressableRowStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? Color(.systemGray5) : Color.clear)
            .animation(.easeInOut(duration: 0.1), value: configuration.isPressed)
    }
}

struct ContentView: View {
    @StateObject private var viewModel = VehicleViewModel()
    @State private var isReceiving = false
    @State private var showESP32Config = false
    private let receiveCheckTimer = Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()

    private var statusColor: Color {
        switch viewModel.connectionState {
        case .connected:
            return .green
        case .connecting:
            return .orange
        case .disconnected:
            return .red
        }
    }

    var body: some View {
        NavigationView {
            Form {
                vehicleSelectionSection
                connectionSection
                esp32DiscoverySection
                detectionSection
                telemetrySection
            }
            .navigationTitle("Vehicle Telemetry")
            .onReceive(receiveCheckTimer) { _ in
                isReceiving = viewModel.isReceivingData
            }
        }
    }

    // MARK: - Vehicle Selection

    private var vehicleSelectionSection: some View {
        Section(header: Text("Vehicle")) {
            Picker("Vehicle Type", selection: $viewModel.selectedVehicle) {
                ForEach(viewModel.vehicleOptions, id: \.0) { option in
                    Text(option.1).tag(option.0)
                }
            }
            .pickerStyle(.segmented)
            .onChange(of: viewModel.selectedVehicle) {
                if viewModel.connectionState == .connected {
                    viewModel.switchVehicleType(viewModel.selectedVehicle)
                }
            }
        }
    }

    // MARK: - Connection

    private var connectionSection: some View {
        Section(header: Text("Connection")) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 6) {
                        if viewModel.connectionState == .connected {
                            Circle()
                                .fill(isReceiving ? .green : .gray)
                                .frame(width: 8, height: 8)
                                .animation(.easeInOut(duration: 0.3), value: isReceiving)
                        }
                        HStack(spacing: 0) {
                            Text(viewModel.connectionStatus)
                                .foregroundColor(statusColor)
                            if viewModel.connectionState == .connecting {
                                ConnectingDotsView()
                            }
                        }
                    }
                    if let deviceName = viewModel.connectedDeviceName {
                        Text(deviceName)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    if let deviceAddress = viewModel.connectedDeviceAddress {
                        Text(deviceAddress)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                Spacer()
            }

            if viewModel.connectionState == .disconnected {
                Button(action: { viewModel.scanForDevices() }) {
                    HStack {
                        if viewModel.isScanning {
                            ProgressView()
                                .scaleEffect(0.8)
                            Text("Scanning...")
                        } else {
                            Text("Scan for BLE Adapters")
                        }
                    }
                }
                .disabled(viewModel.isScanning)
            }

            if viewModel.connectionState == .connected {
                Button("Disconnect") {
                    viewModel.disconnect()
                }
                .foregroundColor(.red)
            }

            if !viewModel.discoveredDevices.isEmpty && viewModel.connectionState == .disconnected {
                ForEach(viewModel.discoveredDevices) { device in
                    Button(action: { viewModel.connectToDevice(device) }) {
                        HStack {
                            VStack(alignment: .leading) {
                                Text(device.name)
                                    .foregroundColor(.primary)
                                Text(device.address)
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                            Spacer()
                            Text("RSSI: \(device.rssi)")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                }
            }
        }
    }

    // MARK: - ESP32 Discovery

    private var esp32DiscoverySection: some View {
        Section(header: Text("ESP32 CAN Bridge")) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 6) {
                        Circle()
                            .fill(viewModel.isESP32DiscoveryActive ? .green : .gray)
                            .frame(width: 8, height: 8)
                        Text(viewModel.isESP32DiscoveryActive
                             ? "Listening for ESP32 broadcasts"
                             : "Discovery inactive")
                            .foregroundColor(viewModel.isESP32DiscoveryActive ? .green : .secondary)
                    }
                    if let error = viewModel.esp32DiscoveryError {
                        Text(error)
                            .font(.caption)
                            .foregroundColor(.red)
                    }
                }
                Spacer()
            }

            Toggle("Auto-connect to first ESP32", isOn: $viewModel.autoConnectEnabled)
                .disabled(viewModel.isESP32DiscoveryActive)

            if !viewModel.isESP32DiscoveryActive {
                Button("Configure and Start Discovery") {
                    showESP32Config = true
                }
            } else {
                Button("Stop Discovery") {
                    viewModel.stopESP32Discovery()
                }
                .foregroundColor(.red)
            }

            if viewModel.discoveredESP32s.isEmpty && viewModel.isESP32DiscoveryActive {
                Text("Waiting for ESP32 broadcast packets...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            ForEach(viewModel.discoveredESP32s) { esp32 in
                ESP32DeviceRow(
                    esp32: esp32,
                    isAutoConnected: viewModel.autoConnectedESP32?.address == esp32.address,
                    isConnected: viewModel.connectionState == .connected
                        && viewModel.connectedDeviceAddress == esp32.canEndpointDescription,
                    onConnect: { viewModel.connectToESP32(esp32) }
                )
            }
        }
        .sheet(isPresented: $showESP32Config) {
            ESP32ConfigSheet(viewModel: viewModel, isPresented: $showESP32Config)
        }
    }

    // MARK: - Detection

    private var detectionSection: some View {
        Group {
            if viewModel.connectionState == .connected {
                Section(header: Text("Vehicle Detection")) {
                    HStack {
                        Circle()
                            .fill(isReceiving ? .green : .gray)
                            .frame(width: 8, height: 8)
                        Text("BLE notifications: \(viewModel.bleNotificationCount)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    if !viewModel.lastRawHex.isEmpty {
                        Text(viewModel.lastRawHex)
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(.secondary)
                    }

                    if !viewModel.detectionInfo.isEmpty {
                        Text(viewModel.detectionInfo)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
            }
        }
    }

    // MARK: - Telemetry

    private var telemetrySection: some View {
        Section(header: Text("Vehicle Telemetry")) {
            LazyVGrid(columns: [
                GridItem(.flexible()),
                GridItem(.flexible())
            ], spacing: 12) {
                TelemetryCardView(
                    title: "Speed",
                    value: viewModel.speed.map { String(format: "%.1f", $0) } ?? "--",
                    unit: "km/h",
                    color: .green
                )

                TelemetryCardView(
                    title: "Gear",
                    value: viewModel.gearSelector ?? "--",
                    unit: "",
                    color: .orange
                )

                TelemetryCardView(
                    title: "Throttle",
                    value: viewModel.throttlePercent.map { String(format: "%.1f%%", $0) } ?? "--",
                    unit: "",
                    color: .blue
                )

                if viewModel.selectedVehicle == "tesla_model3" {
                    TelemetryCardView(
                        title: "Motor RPM",
                        value: viewModel.motorRpm.map { String(format: "%.0f", $0) } ?? "--",
                        unit: "rpm",
                        color: .orange
                    )

                    TelemetryCardView(
                        title: "Torque",
                        value: viewModel.motorTorqueNm.map { String(format: "%.1f", $0) } ?? "--",
                        unit: "Nm",
                        color: .purple
                    )

                    TelemetryCardView(
                        title: "Steering",
                        value: viewModel.steeringAngleDeg.map { String(format: "%.1f", $0) } ?? "--",
                        unit: "deg",
                        color: .cyan
                    )
                }

                TelemetryCardView(
                    title: "Acceleration",
                    value: viewModel.acceleration.map { String(format: "%.2f", $0) } ?? "--",
                    unit: "g",
                    color: .red
                )

                TelemetryCardView(
                    title: "Brake",
                    value: viewModel.brakePercent.map { String(format: "%.1f%%", $0) } ?? "--",
                    unit: "",
                    color: .yellow
                )
            }
        }
    }
}

// MARK: - ESP32 Device Row

private struct ESP32DeviceRow: View {
    let esp32: DiscoveredESP32
    let isAutoConnected: Bool
    let isConnected: Bool
    let onConnect: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("ESP32")
                    .fontWeight(.medium)
                if isAutoConnected {
                    Text("AUTO")
                        .font(.caption2)
                        .padding(.horizontal, 4)
                        .padding(.vertical, 1)
                        .background(Color.green.opacity(0.2))
                        .cornerRadius(3)
                }
                Spacer()
                if isConnected {
                    Text("CONNECTED")
                        .font(.caption2)
                        .padding(.horizontal, 4)
                        .padding(.vertical, 1)
                        .background(Color.blue.opacity(0.2))
                        .cornerRadius(3)
                } else {
                    Button("Connect", action: onConnect)
                        .font(.caption)
                        .buttonStyle(.borderedProminent)
                        .controlSize(.small)
                }
            }
            Text(esp32.canEndpointDescription)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
            Text("Discovered: \(esp32.receivedAt, formatter: timeFormatter)")
                .font(.caption2)
                .foregroundColor(.secondary)
        }
        .padding(.vertical, 2)
    }
}

// MARK: - ESP32 Configuration Sheet

private struct ESP32ConfigSheet: View {
    @ObservedObject var viewModel: VehicleViewModel
    @Binding var isPresented: Bool
    @State private var deviceIdHex = ""
    @State private var publicKeyHex = ""
    @State private var errorMessage: String?

    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("Trusted Device ID"),
                        footer: Text("16-byte hex string identifying your ESP32 (e.g. from ESP32 serial output on boot)")) {
                    TextField("Device ID (32 hex chars)", text: $deviceIdHex)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                }

                Section(header: Text("Verification Public Key"),
                        footer: Text("32-byte Ed25519 public key in hex (64 hex chars). Must match the key baked into the ESP32 firmware.")) {
                    TextField("Public Key (64 hex chars)", text: $publicKeyHex)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                }

                if let error = errorMessage {
                    Section {
                        Text(error)
                            .foregroundColor(.red)
                            .font(.caption)
                    }
                }

                Section {
                    Button("Start Discovery") {
                        startDiscovery()
                    }
                    .disabled(deviceIdHex.isEmpty || publicKeyHex.isEmpty)
                }
            }
            .navigationTitle("ESP32 Discovery")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { isPresented = false }
                }
            }
        }
    }

    private func startDiscovery() {
        errorMessage = nil

        guard let deviceId = Data(hex: deviceIdHex), deviceId.count == 16 else {
            errorMessage = "Device ID must be exactly 32 hex characters (16 bytes)."
            return
        }

        guard let publicKeyData = Data(hex: publicKeyHex), publicKeyData.count == 32 else {
            errorMessage = "Public key must be exactly 64 hex characters (32 bytes)."
            return
        }

        do {
            let publicKey = try Curve25519.Signing.PublicKey(rawRepresentation: publicKeyData)
            viewModel.startESP32Discovery(trustedDeviceId: deviceId, publicKey: publicKey)
            isPresented = false
        } catch {
            errorMessage = "Invalid public key: \(error.localizedDescription)"
        }
    }
}

// MARK: - Data hex conversion

private extension Data {
    init?(hex: String) {
        let cleaned = hex.replacingOccurrences(of: " ", with: "")
        guard cleaned.count % 2 == 0 else { return nil }
        var data = Data()
        var index = cleaned.startIndex
        while index < cleaned.endIndex {
            let nextIndex = cleaned.index(index, offsetBy: 2)
            let byteString = String(cleaned[index..<nextIndex])
            guard let byte = UInt8(byteString, radix: 16) else { return nil }
            data.append(byte)
            index = nextIndex
        }
        self = data
    }
}

// MARK: - Formatters

private let timeFormatter: DateFormatter = {
    let formatter = DateFormatter()
    formatter.dateStyle = .none
    formatter.timeStyle = .medium
    return formatter
}()

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
