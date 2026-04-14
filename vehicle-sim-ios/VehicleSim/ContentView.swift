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
                    Text(viewModel.isConnected ? "Showing demo data" : "Tap Start for demo")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }

                Section(header: Text("Vehicle Telemetry")) {
                    TelemetryCardView(
                        title: "Throttle",
                        value: String(format: "%.1f%%", viewModel.throttlePercent),
                        unit: "",
                        color: .blue
                    )
                    TelemetryCardView(
                        title: "Speed",
                        value: String(format: "%.1f", viewModel.speed),
                        unit: "km/h",
                        color: .green
                    )
                    TelemetryCardView(
                        title: "Acceleration",
                        value: String(format: "%.2f", viewModel.acceleration),
                        unit: "g",
                        color: .red
                    )
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
