import Foundation

/// Swift representation of vehicle telemetry data
/// Mirrors the C++ vehicle_sim::domain::PhysicsData structure
struct TelemetryData {
    let timestamp: TimeInterval
    let rpm: Double
    let speedKmh: Double
    let throttlePercent: Double
    let brakePercent: Double
    let gear: Int
    let torque: Double
    let accelerationG: Double
}
