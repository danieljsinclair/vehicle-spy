import Foundation
import Combine

/// Observable view model for SwiftUI telemetry display.
/// Maps directly from C++ VehicleSignal via VehicleSimWrapper.
class VehicleViewModel: ObservableObject {
    // MARK: - Published Properties (match VehicleSignal exactly)

    @Published var throttlePercent: Double = 0.0
    @Published var speed: Double = 0.0
    @Published var acceleration: Double = 0.0
    @Published var brakePercent: Double = 0.0

    @Published var isConnected: Bool = false
    @Published var connectionStatus: String = "Disconnected"

    // MARK: - Private

    private var wrapper: VehicleSimWrapper?
    private var updateTimer: Timer?
    private let updateInterval: TimeInterval = 0.1 // 10 Hz

    // MARK: - Lifecycle

    init() {
        wrapper = VehicleSimWrapper()
    }

    deinit {
        stopUpdates()
    }

    // MARK: - Public API

    func start() {
        guard wrapper != nil else {
            connectionStatus = "Wrapper not initialized"
            return
        }

        wrapper?.start()
        isConnected = true
        connectionStatus = "Connected (Demo)"

        updateTimer = Timer.scheduledTimer(
            withTimeInterval: updateInterval,
            repeats: true
        ) { [weak self] _ in
            self?.updateTelemetry()
        }
    }

    func stop() {
        wrapper?.stop()
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

        wrapper.update()
        self.throttlePercent = wrapper.throttlePercent
        self.speed = wrapper.speedKmh
        self.acceleration = wrapper.accelerationG
        self.brakePercent = wrapper.brakePercent
    }
}
