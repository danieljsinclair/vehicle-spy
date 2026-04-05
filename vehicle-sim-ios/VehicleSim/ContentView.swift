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
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }
}

struct ContentView: View {
    @StateObject private var viewModel = VehicleViewModel()

    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("Connection")) {
                    HStack {
                        Text("Status")
                        Spacer()
                        Text(viewModel.connectionStatus)
                            .foregroundColor(viewModel.isConnected ? .green : .red)
                    }
                }

                Section(header: Text("Engine")) {
                    TelemetryCardView(
                        title: "RPM",
                        value: String(format: "%.0f", viewModel.rpm),
                        unit: "rev/min",
                        color: .orange
                    )
                    TelemetryCardView(
                        title: "Throttle",
                        value: String(format: "%.1f%%", viewModel.throttlePercent),
                        unit: "",
                        color: .blue
                    )
                }

                Section(header: Text("Performance")) {
                    TelemetryCardView(
                        title: "Speed",
                        value: String(format: "%.1f", viewModel.speed),
                        unit: "km/h",
                        color: .green
                    )
                    TelemetryCardView(
                        title: "Torque",
                        value: String(format: "%.1f", viewModel.torque),
                        unit: "Nm",
                        color: .purple
                    )
                    TelemetryCardView(
                        title: "Acceleration",
                        value: String(format: "%.2f", viewModel.acceleration),
                        unit: "g",
                        color: .red
                    )
                }

                Section(header: Text("Drivetrain")) {
                    HStack {
                        Text("Gear")
                        Spacer()
                        Text("\(viewModel.gear)")
                            .font(.title3)
                            .foregroundColor(.primary)
                    }
                    TelemetryCardView(
                        title: "Brake",
                        value: String(format: "%.1f%%", viewModel.brakePercent),
                        unit: "",
                        color: .yellow
                    )
                }
            }
            .navigationTitle("Vehicle Telemetry")
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: {
                        if viewModel.isConnected {
                            viewModel.stop()
                        } else {
                            viewModel.start()
                        }
                    }) {
                        Text(viewModel.isConnected ? "Stop" : "Start")
                            .foregroundColor(viewModel.isConnected ? .red : .green)
                    }
                }
            }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
