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
     * Parameter object grouping the (mostly optional) signal fields that make
     * up a VehicleSignal. Bundles the constructor's 11 arguments into one
     * object so call sites read by name and stay resilient to field order.
     * Every field defaults to nullopt except timestampUtcMs.
     */
    struct Params {
        std::uint64_t timestampUtcMs = 0;
        std::optional<double> throttlePercent;
        std::optional<double> speedKmh;
        std::optional<double> accelerationG;
        std::optional<double> brakePercent;
        std::optional<double> steeringAngleDeg;
        std::optional<double> motorRpm;
        std::optional<double> motorHvVoltage;
        std::optional<double> motorHvCurrent;
        std::optional<double> motorTorqueNm;
        std::optional<std::int32_t> gearSelector;
    };

    /**
     * Construct a VehicleSignal from a Params object (cpp:S107 parameter object
     * replacing the 11-argument constructor).
     */
    explicit VehicleSignal(Params params) noexcept;

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
    [[nodiscard]] const std::optional<std::int32_t>& getGearSelector() const noexcept;
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
    [[nodiscard]] std::int32_t gearSelectorOr(std::int32_t defaultVal = 0) const noexcept;

    // Equality comparison (member-wise, uses optional's ==)
    [[nodiscard]] friend bool operator==(const VehicleSignal& lhs, const VehicleSignal& rhs) noexcept {
        return lhs.m_throttlePercent == rhs.m_throttlePercent &&
               lhs.m_speedKmh == rhs.m_speedKmh &&
               lhs.m_accelerationG == rhs.m_accelerationG &&
               lhs.m_brakePercent == rhs.m_brakePercent &&
               lhs.m_steeringAngleDeg == rhs.m_steeringAngleDeg &&
               lhs.m_motorRpm == rhs.m_motorRpm &&
               lhs.m_motorHvVoltage == rhs.m_motorHvVoltage &&
               lhs.m_motorHvCurrent == rhs.m_motorHvCurrent &&
               lhs.m_motorTorqueNm == rhs.m_motorTorqueNm &&
               lhs.m_gearSelector == rhs.m_gearSelector &&
               lhs.m_timestampUtcMs == rhs.m_timestampUtcMs;
    }

    // Inequality (derived from operator==)
    [[nodiscard]] friend bool operator!=(const VehicleSignal& lhs, const VehicleSignal& rhs) noexcept {
        return !(lhs == rhs);
    }

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
    std::optional<std::int32_t> m_gearSelector;
    std::uint64_t              m_timestampUtcMs;
};

} // namespace vehicle_sim::domain
