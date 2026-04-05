import Foundation
import Combine

// Bridge to C++ core - will be provided by ObjC++ wrapper
// @objc class VehicleSimWrapper: NSObject { ... }

/// Observable view model for SwiftUI telemetry display
class VehicleViewModel: ObservableObject {
    // MARK: - Published Properties for UI

    @Published var rpm: Double = 0.0
    @Published var speed: Double = 0.0
    @Published var throttlePercent: Double = 0.0
    @Published var brakePercent: Double = 0.0
    @Published var gear: Int = 0
    @Published var torque: Double = 0.0
    @Published var acceleration: Double = 0.0

    @Published var isConnected: Bool = false
    @Published var connectionStatus: String = "Disconnected"

    // MARK: - Private

    private var wrapper: VehicleSimWrapper?
    private var updateTimer: Timer?
    private let updateInterval: TimeInterval = 0.1 // 10 Hz

    // MARK: - Lifecycle

    init() {
        // Initialize C++ wrapper
        wrapper = VehicleSimWrapper()
    }

    deinit {
        stopUpdates()
    }

    // MARK: - Public API

    /// Start the telemetry stream (connect to BLE mock)
    func start() {
        guard wrapper != nil else {
            connectionStatus = "Wrapper not initialized"
            return
        }

        isConnected = true
        connectionStatus = "Connected (Mock)"

        // Start periodic updates
        updateTimer = Timer.scheduledTimer(
            withTimeInterval: updateInterval,
            repeats: true
        ) { [weak self] _ in
            // Timer fires on main run loop, safe to call directly
            self?.updateTelemetry()
        }
    }

    /// Stop the telemetry stream
    func stop() {
        stopUpdates()
        isConnected = false
        connectionStatus = "Disconnected"
    }

    // MARK: - Private

    private func stopUpdates() {
        updateTimer?.invalidate()
        updateTimer = nil
    }

    private func updateTelemetry() {
        guard let wrapper = wrapper else { return }

        // Get latest telemetry from C++ core through wrapper
        if let data = wrapper.getTelemetry() {
            // Update published properties (on main actor)
            self.rpm = data.rpm
            self.speed = data.speedKmh
            self.throttlePercent = data.throttlePercent
            self.brakePercent = data.brakePercent
            self.gear = Int(data.gear)
            self.torque = data.torque
            self.acceleration = data.accelerationG
        }
    }
}
