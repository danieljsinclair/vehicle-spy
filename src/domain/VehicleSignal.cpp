#include "vehicle-sim/domain/VehicleSignal.h"

#include <algorithm>

namespace vehicle_sim::domain {

VehicleSignal::VehicleSignal(
    double throttlePercent,
    double speedKmh,
    double accelerationG,
    double brakePercent,
    std::uint64_t timestampUtcMs,
    double steeringAngleDeg,
    double motorRpm,
    double motorHvVoltage,
    double motorHvCurrent,
    double motorTorqueNm,
    std::string gearSelector
) noexcept
    : m_throttlePercent(std::clamp(throttlePercent, THROTTLE_MIN, THROTTLE_MAX))
    , m_speedKmh(std::clamp(speedKmh, SPEED_MIN, SPEED_MAX))
    , m_accelerationG(std::clamp(accelerationG, ACCEL_MIN, ACCEL_MAX))
    , m_brakePercent(std::clamp(brakePercent, BRAKE_MIN, BRAKE_MAX))
    , m_steeringAngleDeg(std::clamp(steeringAngleDeg, STEERING_MIN, STEERING_MAX))
    , m_motorRpm(std::clamp(motorRpm, MOTOR_RPM_MIN, MOTOR_RPM_MAX))
    , m_motorHvVoltage(std::clamp(motorHvVoltage, HV_VOLTAGE_MIN, HV_VOLTAGE_MAX))
    , m_motorHvCurrent(std::clamp(motorHvCurrent, HV_CURRENT_MIN, HV_CURRENT_MAX))
    , m_motorTorqueNm(std::clamp(motorTorqueNm, MOTOR_TORQUE_MIN, MOTOR_TORQUE_MAX))
    , m_gearSelector(std::move(gearSelector))
    , m_timestampUtcMs(timestampUtcMs)
{
}

double VehicleSignal::getThrottlePercent() const noexcept
{
    return m_throttlePercent;
}

double VehicleSignal::getSpeedKmh() const noexcept
{
    return m_speedKmh;
}

double VehicleSignal::getAccelerationG() const noexcept
{
    return m_accelerationG;
}

double VehicleSignal::getBrakePercent() const noexcept
{
    return m_brakePercent;
}

double VehicleSignal::getSteeringAngleDeg() const noexcept
{
    return m_steeringAngleDeg;
}

double VehicleSignal::getMotorRpm() const noexcept
{
    return m_motorRpm;
}

double VehicleSignal::getMotorHvVoltage() const noexcept
{
    return m_motorHvVoltage;
}

double VehicleSignal::getMotorHvCurrent() const noexcept
{
    return m_motorHvCurrent;
}

double VehicleSignal::getMotorTorqueNm() const noexcept
{
    return m_motorTorqueNm;
}

const std::string& VehicleSignal::getGearSelector() const noexcept
{
    return m_gearSelector;
}

std::uint64_t VehicleSignal::getTimestampUtcMs() const noexcept
{
    return m_timestampUtcMs;
}

bool VehicleSignal::operator==(const VehicleSignal& other) const noexcept
{
    return m_throttlePercent == other.m_throttlePercent &&
           m_speedKmh == other.m_speedKmh &&
           m_accelerationG == other.m_accelerationG &&
           m_brakePercent == other.m_brakePercent &&
           m_steeringAngleDeg == other.m_steeringAngleDeg &&
           m_motorRpm == other.m_motorRpm &&
           m_motorHvVoltage == other.m_motorHvVoltage &&
           m_motorHvCurrent == other.m_motorHvCurrent &&
           m_motorTorqueNm == other.m_motorTorqueNm &&
           m_gearSelector == other.m_gearSelector &&
           m_timestampUtcMs == other.m_timestampUtcMs;
}

bool VehicleSignal::operator!=(const VehicleSignal& other) const noexcept
{
    return !(*this == other);
}

} // namespace vehicle_sim::domain
