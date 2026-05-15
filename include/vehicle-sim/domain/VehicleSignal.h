#pragma once

#include <cstdint>
#include <string>

namespace vehicle_sim::domain {

/**
 * Immutable canonical vehicle signal value object
 *
 * Standard OBD2 normalized format. All values are in standard units.
 * This is the only signal format that crosses the boundary layer.
 * No Tesla specific values, units or encodings exist in this type.
 *
 * All fields are guaranteed valid on construction.
 */
class VehicleSignal final {
public:
    /**
     * Construct a valid VehicleSignal
     * All parameters are clamped to valid ranges
     */
    VehicleSignal(
        double throttlePercent,
        double speedKmh,
        double accelerationG,
        double brakePercent,
        std::uint64_t timestampUtcMs,
        double steeringAngleDeg = 0.0,
        double motorRpm = 0.0,
        double motorHvVoltage = 0.0,
        double motorHvCurrent = 0.0,
        double motorTorqueNm = 0.0,
        std::string gearSelector = ""
    ) noexcept;

    // Default copy / move
    VehicleSignal(const VehicleSignal&) noexcept = default;
    VehicleSignal(VehicleSignal&&) noexcept = default;
    VehicleSignal& operator=(const VehicleSignal&) noexcept = default;
    VehicleSignal& operator=(VehicleSignal&&) noexcept = default;
    ~VehicleSignal() noexcept = default;

    // Value accessors
    [[nodiscard]] double getThrottlePercent() const noexcept;
    [[nodiscard]] double getSpeedKmh() const noexcept;
    [[nodiscard]] double getAccelerationG() const noexcept;
    [[nodiscard]] double getBrakePercent() const noexcept;
    [[nodiscard]] double getSteeringAngleDeg() const noexcept;
    [[nodiscard]] double getMotorRpm() const noexcept;
    [[nodiscard]] double getMotorHvVoltage() const noexcept;
    [[nodiscard]] double getMotorHvCurrent() const noexcept;
    [[nodiscard]] double getMotorTorqueNm() const noexcept;
    [[nodiscard]] const std::string& getGearSelector() const noexcept;
    [[nodiscard]] std::uint64_t getTimestampUtcMs() const noexcept;

    // Equality comparison (member-wise)
    [[nodiscard]] bool operator==(const VehicleSignal& other) const noexcept;

    // Inequality (derived from operator==)
    [[nodiscard]] bool operator!=(const VehicleSignal& other) const noexcept;

    // Valid signal ranges (DBC-verified)
    static constexpr double THROTTLE_MIN = 0.0;
    static constexpr double THROTTLE_MAX = 100.0;
    static constexpr double SPEED_MIN = 0.0;
    static constexpr double SPEED_MAX = 300.0;
    static constexpr double ACCEL_MIN = -5.0;
    static constexpr double ACCEL_MAX = 5.0;
    static constexpr double BRAKE_MIN = 0.0;
    static constexpr double BRAKE_MAX = 100.0;
    static constexpr double STEERING_MIN = -819.2;  // DBC: SCCM 14-bit range
    static constexpr double STEERING_MAX = 819.2;
    static constexpr double MOTOR_RPM_MIN = 0.0;
    static constexpr double MOTOR_RPM_MAX = 20000.0;       // Tesla CMPD DBC
    static constexpr double HV_VOLTAGE_MIN = 0.0;
    static constexpr double HV_VOLTAGE_MAX = 1000.0;       // Tesla CMPD DBC
    static constexpr double HV_CURRENT_MIN = 0.0;
    static constexpr double HV_CURRENT_MAX = 50.0;         // Tesla CMPD DBC
    static constexpr double MOTOR_TORQUE_MIN = -7500.0;    // Tesla DIR_torqueActual
    static constexpr double MOTOR_TORQUE_MAX = 7500.0;

private:
    double      m_throttlePercent;
    double      m_speedKmh;
    double      m_accelerationG;
    double      m_brakePercent;
    double      m_steeringAngleDeg;
    double      m_motorRpm;
    double      m_motorHvVoltage;
    double      m_motorHvCurrent;
    double      m_motorTorqueNm;
    std::string m_gearSelector;
    std::uint64_t m_timestampUtcMs;
};

} // namespace vehicle_sim::domain
