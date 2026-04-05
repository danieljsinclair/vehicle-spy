#include "vehicle-sim/domain/VehicleSignal.h"

#include <algorithm>

namespace vehicle_sim::domain {

VehicleSignal::VehicleSignal(
    double throttlePercent,
    double speedKmh,
    double accelerationG,
    double brakePercent,
    std::uint64_t timestampUtcMs
) noexcept
    : m_throttlePercent(std::clamp(throttlePercent, 0.0, 100.0))
    , m_speedKmh(std::clamp(speedKmh, 0.0, 300.0))
    , m_accelerationG(std::clamp(accelerationG, -5.0, 5.0))
    , m_brakePercent(std::clamp(brakePercent, 0.0, 100.0))
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
           m_timestampUtcMs == other.m_timestampUtcMs;
}

bool VehicleSignal::operator!=(const VehicleSignal& other) const noexcept
{
    return !(*this == other);
}

} // namespace vehicle_sim::domain
