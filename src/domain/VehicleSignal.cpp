#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

VehicleSignal::VehicleSignal(Params params) noexcept
    : m_throttlePercent(std::move(params.throttlePercent))
    , m_speedKmh(std::move(params.speedKmh))
    , m_accelerationG(std::move(params.accelerationG))
    , m_brakePercent(std::move(params.brakePercent))
    , m_steeringAngleDeg(std::move(params.steeringAngleDeg))
    , m_motorRpm(std::move(params.motorRpm))
    , m_motorHvVoltage(std::move(params.motorHvVoltage))
    , m_motorHvCurrent(std::move(params.motorHvCurrent))
    , m_motorTorqueNm(std::move(params.motorTorqueNm))
    , m_gearSelector(std::move(params.gearSelector))
    , m_timestampUtcMs(params.timestampUtcMs)
{
}

const std::optional<double>& VehicleSignal::getThrottlePercent() const noexcept
{
    return m_throttlePercent;
}

const std::optional<double>& VehicleSignal::getSpeedKmh() const noexcept
{
    return m_speedKmh;
}

const std::optional<double>& VehicleSignal::getAccelerationG() const noexcept
{
    return m_accelerationG;
}

const std::optional<double>& VehicleSignal::getBrakePercent() const noexcept
{
    return m_brakePercent;
}

const std::optional<double>& VehicleSignal::getSteeringAngleDeg() const noexcept
{
    return m_steeringAngleDeg;
}

const std::optional<double>& VehicleSignal::getMotorRpm() const noexcept
{
    return m_motorRpm;
}

const std::optional<double>& VehicleSignal::getMotorHvVoltage() const noexcept
{
    return m_motorHvVoltage;
}

const std::optional<double>& VehicleSignal::getMotorHvCurrent() const noexcept
{
    return m_motorHvCurrent;
}

const std::optional<double>& VehicleSignal::getMotorTorqueNm() const noexcept
{
    return m_motorTorqueNm;
}

const std::optional<std::int32_t>& VehicleSignal::getGearSelector() const noexcept
{
    return m_gearSelector;
}

std::uint64_t VehicleSignal::getTimestampUtcMs() const noexcept
{
    return m_timestampUtcMs;
}

double VehicleSignal::throttlePercentOr(double defaultVal) const noexcept { return m_throttlePercent.value_or(defaultVal); }
double VehicleSignal::speedKmhOr(double defaultVal) const noexcept { return m_speedKmh.value_or(defaultVal); }
double VehicleSignal::accelerationGOr(double defaultVal) const noexcept { return m_accelerationG.value_or(defaultVal); }
double VehicleSignal::brakePercentOr(double defaultVal) const noexcept { return m_brakePercent.value_or(defaultVal); }
double VehicleSignal::steeringAngleDegOr(double defaultVal) const noexcept { return m_steeringAngleDeg.value_or(defaultVal); }
double VehicleSignal::motorRpmOr(double defaultVal) const noexcept { return m_motorRpm.value_or(defaultVal); }
double VehicleSignal::motorHvVoltageOr(double defaultVal) const noexcept { return m_motorHvVoltage.value_or(defaultVal); }
double VehicleSignal::motorHvCurrentOr(double defaultVal) const noexcept { return m_motorHvCurrent.value_or(defaultVal); }
double VehicleSignal::motorTorqueNmOr(double defaultVal) const noexcept { return m_motorTorqueNm.value_or(defaultVal); }
std::int32_t VehicleSignal::gearSelectorOr(std::int32_t defaultVal) const noexcept { return m_gearSelector.value_or(defaultVal); }

} // namespace vehicle_sim::domain
