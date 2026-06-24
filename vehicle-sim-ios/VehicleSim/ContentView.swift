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

struct ContentView: View {
    @StateObject private var viewModel = VehicleViewModel()
    @State private var isReceiving = false
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
                connectionModeSection
                connectionSection
                esp32DevicesSection
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
            .onChange(of: viewModel.selectedVehicle) { _, _ in
                if viewModel.connectionState == .connected {
                    viewModel.switchVehicleType(viewModel.selectedVehicle)
                }
            }
        }
    }

    // MARK: - Connection Mode

    private var connectionModeSection: some View {
        Section(header: Text("Mode")) {
            Picker("Connection Mode", selection: $viewModel.connectionMode) {
                ForEach(ConnectionMode.allCases, id: \.self) { mode in
                    Text(mode.rawValue).tag(mode)
                }
            }
            .pickerStyle(.segmented)
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

            if viewModel.connectionMode == .ble {
                bleConnectionControls
            }

            if viewModel.connectionState == .connected {
                Button("Disconnect") {
                    viewModel.disconnect()
                }
                .foregroundColor(.red)
            }
        }
    }

    @ViewBuilder
    private var bleConnectionControls: some View {
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

    // MARK: - ESP32 Discovered Devices (WiFi mode)

    @ViewBuilder
    private var esp32DevicesSection: some View {
        if viewModel.connectionMode == .wifi {
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
                        if let securityError = viewModel.wifiSecurityError {
                            Text(securityError)
                                .font(.caption)
                                .foregroundColor(.orange)
                        }
                    }
                    Spacer()
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
                        verificationState: viewModel.wifiSecurityPolicy.verificationState(for: esp32.deviceId),
                        onConnect: { viewModel.connectToESP32(esp32) },
                        onTrust: { viewModel.trustESP32(esp32) }
                    )
                }
            }
        }
    }

    // MARK: - Detection

    @ViewBuilder
    private var detectionSection: some View {
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
    let verificationState: DeviceVerificationState
    let onConnect: () -> Void
    let onTrust: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("ESP32")
                    .fontWeight(.medium)
                verificationBadge
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
                } else if verificationState == .rejected {
                    Text("REJECTED")
                        .font(.caption2)
                        .padding(.horizontal, 4)
                        .padding(.vertical, 1)
                        .background(Color.red.opacity(0.2))
                        .cornerRadius(3)
                }
            }
            Text(esp32.canEndpointDescription)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
            HStack {
                Text("Discovered: \(esp32.receivedAt, formatter: timeFormatter)")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                Spacer()
                if !isConnected && verificationState == .unverified {
                    Button("Trust", action: onTrust)
                        .font(.caption)
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                }
                if !isConnected && (verificationState == .verified || verificationState == .userTrusted) {
                    Button("Connect", action: onConnect)
                        .font(.caption)
                        .buttonStyle(.borderedProminent)
                        .controlSize(.small)
                }
            }
        }
        .padding(.vertical, 2)
    }

    @ViewBuilder
    private var verificationBadge: some View {
        switch verificationState {
        case .verified:
            Text("VERIFIED")
                .font(.caption2)
                .padding(.horizontal, 4)
                .padding(.vertical, 1)
                .background(Color.green.opacity(0.2))
                .cornerRadius(3)
        case .rejected:
            Text("UNTRUSTED")
                .font(.caption2)
                .padding(.horizontal, 4)
                .padding(.vertical, 1)
                .background(Color.red.opacity(0.2))
                .cornerRadius(3)
        case .userTrusted:
            Text("TRUSTED")
                .font(.caption2)
                .padding(.horizontal, 4)
                .padding(.vertical, 1)
                .background(Color.orange.opacity(0.2))
                .cornerRadius(3)
        case .unverified:
            Text("?")
                .font(.caption2)
                .padding(.horizontal, 4)
                .padding(.vertical, 1)
                .background(Color.yellow.opacity(0.2))
                .cornerRadius(3)
        }
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
