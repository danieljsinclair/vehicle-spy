#include "vehicle-sim/domain/TelemetrySignal.h"
#include "vehicle-sim/domain/Gear.h"

#include <algorithm>

namespace vehicle_sim::domain {

TelemetrySignal::TelemetrySignal(
    double rpm,
    int gear,
    double torqueNm,
    double speedKmh,
    double throttlePercent,
    std::uint64_t timestampUtcMs
) noexcept
    : m_rpm(std::clamp(rpm, 0.0, 12000.0))
    , m_gear(std::clamp(gear, Gear::REVERSE, Gear::GEAR_6))
    , m_torqueNm(std::clamp(torqueNm, 0.0, 1500.0))
    , m_speedKmh(std::clamp(speedKmh, 0.0, 400.0))
    , m_throttlePercent(std::clamp(throttlePercent, 0.0, 100.0))
    , m_timestampUtcMs(timestampUtcMs)
{
}

double TelemetrySignal::getRpm() const noexcept
{
    return m_rpm;
}

int TelemetrySignal::getGear() const noexcept
{
    return m_gear;
}

double TelemetrySignal::getTorqueNm() const noexcept
{
    return m_torqueNm;
}

double TelemetrySignal::getSpeedKmh() const noexcept
{
    return m_speedKmh;
}

double TelemetrySignal::getThrottlePercent() const noexcept
{
    return m_throttlePercent;
}

std::uint64_t TelemetrySignal::getTimestampUtcMs() const noexcept
{
    return m_timestampUtcMs;
}

bool TelemetrySignal::operator==(const TelemetrySignal& other) const noexcept
{
    return m_rpm == other.m_rpm &&
           m_gear == other.m_gear &&
           m_torqueNm == other.m_torqueNm &&
           m_speedKmh == other.m_speedKmh &&
           m_throttlePercent == other.m_throttlePercent &&
           m_timestampUtcMs == other.m_timestampUtcMs;
}

bool TelemetrySignal::operator!=(const TelemetrySignal& other) const noexcept
{
    return !(*this == other);
}

} // namespace vehicle_sim::domain
