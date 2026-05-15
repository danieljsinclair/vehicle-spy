#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace vehicle_sim::domain {

/**
 * Immutable canonical vehicle signal value object
 *
 * Standard OBD2 normalized format. All values are in standard units.
 * This is the only signal format that crosses the boundary layer.
 * No Tesla specific values, units or encodings exist in this type.
 *
 * Fields are optional - only present when the data source provides them.
 */
class VehicleSignal final {
public:
    /**
     * Construct a VehicleSignal
     * All parameters default to nullopt except timestampUtcMs
     */
    VehicleSignal(
        std::uint64_t timestampUtcMs,
        std::optional<double> throttlePercent = std::nullopt,
        std::optional<double> speedKmh = std::nullopt,
        std::optional<double> accelerationG = std::nullopt,
        std::optional<double> brakePercent = std::nullopt,
        std::optional<double> steeringAngleDeg = std::nullopt,
        std::optional<double> motorRpm = std::nullopt,
        std::optional<double> motorHvVoltage = std::nullopt,
        std::optional<double> motorHvCurrent = std::nullopt,
        std::optional<double> motorTorqueNm = std::nullopt,
        std::optional<std::string> gearSelector = std::nullopt
    ) noexcept;

    // Default copy / move
    VehicleSignal(const VehicleSignal&) noexcept = default;
    VehicleSignal(VehicleSignal&&) noexcept = default;
    VehicleSignal& operator=(const VehicleSignal&) noexcept = default;
    VehicleSignal& operator=(VehicleSignal&&) noexcept = default;
    ~VehicleSignal() noexcept = default;

    // Value accessors - return optional
    [[nodiscard]] const std::optional<double>& getThrottlePercent() const noexcept;
    [[nodiscard]] const std::optional<double>& getSpeedKmh() const noexcept;
    [[nodiscard]] const std::optional<double>& getAccelerationG() const noexcept;
    [[nodiscard]] const std::optional<double>& getBrakePercent() const noexcept;
    [[nodiscard]] const std::optional<double>& getSteeringAngleDeg() const noexcept;
    [[nodiscard]] const std::optional<double>& getMotorRpm() const noexcept;
    [[nodiscard]] const std::optional<double>& getMotorHvVoltage() const noexcept;
    [[nodiscard]] const std::optional<double>& getMotorHvCurrent() const noexcept;
    [[nodiscard]] const std::optional<double>& getMotorTorqueNm() const noexcept;
    [[nodiscard]] const std::optional<std::string>& getGearSelector() const noexcept;
    [[nodiscard]] std::uint64_t getTimestampUtcMs() const noexcept;

    // Convenience accessors - return value or default
    [[nodiscard]] double throttlePercentOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double speedKmhOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double accelerationGOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double brakePercentOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double steeringAngleDegOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double motorRpmOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double motorHvVoltageOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double motorHvCurrentOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] double motorTorqueNmOr(double defaultVal = 0.0) const noexcept;
    [[nodiscard]] std::string gearSelectorOr(std::string defaultVal = "") const noexcept;

    // Equality comparison (member-wise, uses optional's ==)
    [[nodiscard]] bool operator==(const VehicleSignal& other) const noexcept;

    // Inequality (derived from operator==)
    [[nodiscard]] bool operator!=(const VehicleSignal& other) const noexcept;

    // Valid signal ranges (DBC-verified) - kept for reference
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
    std::optional<double>      m_throttlePercent;
    std::optional<double>      m_speedKmh;
    std::optional<double>      m_accelerationG;
    std::optional<double>      m_brakePercent;
    std::optional<double>      m_steeringAngleDeg;
    std::optional<double>      m_motorRpm;
    std::optional<double>      m_motorHvVoltage;
    std::optional<double>      m_motorHvCurrent;
    std::optional<double>      m_motorTorqueNm;
    std::optional<std::string> m_gearSelector;
    std::uint64_t              m_timestampUtcMs;
};

} // namespace vehicle_sim::domain
