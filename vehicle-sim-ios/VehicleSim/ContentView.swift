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

private struct DeviceRowView: View {
    let device: VehicleViewModel.DeviceEntry
    let action: () -> Void
    @State private var pressed = false

    var body: some View {
        Button(action: action) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
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
            .contentShape(Rectangle())
        }
        .buttonStyle(PressableRowStyle())
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
                // MARK: - Vehicle Selection
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

                // MARK: - Connection
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
                }

                // MARK: - Discovered Devices
                if !viewModel.discoveredDevices.isEmpty && viewModel.connectionState == .disconnected {
                    Section(header: Text("Discovered Devices")) {
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

                // MARK: - Detection
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

                // MARK: - Telemetry
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

                        // Tesla-specific signals (motor RPM, torque, steering)
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
            .navigationTitle("Vehicle Telemetry")
            .onReceive(receiveCheckTimer) { _ in
                isReceiving = viewModel.isReceivingData
            }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
