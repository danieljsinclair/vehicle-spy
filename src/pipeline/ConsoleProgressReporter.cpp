#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/domain/Gear.h"

#include <iomanip>
#include <ios>
#include <optional>
#include <sstream>
#include <string>

namespace vehicle_sim::pipeline {

namespace {

std::string formatOptional(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

std::string formatOptional(const std::optional<double>& value) {
    return value ? formatOptional(*value) : "";
}

std::string gearLabel(const std::optional<std::int32_t>& gear) {
    return gear ? domain::Gear::labelOr(*gear, std::to_string(*gear)) : "";
}

} // namespace

ConsoleProgressReporter::ConsoleProgressReporter(std::ostream& out, std::string vehicleId) noexcept
    : out_(out)
    , vehicleId_(std::move(vehicleId))
{
}

void ConsoleProgressReporter::onFrame(
    const domain::VehicleSignal& signal,
    std::size_t frameIndex,
    std::size_t totalHints
) noexcept {
    emit(signal, frameIndex, totalHints);
    lastEmittedFrame_ = frameIndex;
}

void ConsoleProgressReporter::onComplete(const ReplayStats& stats) noexcept {
    (void)stats;
}

void ConsoleProgressReporter::emit(
    const domain::VehicleSignal& signal,
    std::size_t frameIndex,
    std::size_t totalHints
) noexcept {
    out_ << std::fixed << std::setprecision(2);
    out_ << "frame " << (frameIndex + 1);
    if (totalHints > 0) {
        const double pct = 100.0 * static_cast<double>(frameIndex + 1)
                           / static_cast<double>(totalHints);
        out_ << " (" << std::fixed << std::setprecision(1) << pct << "%)";
    }
    out_ << "  timestamp_ms=" << signal.getTimestampUtcMs()
         << "  vehicle_id=" << vehicleId_
         << "  speed_kmh=" << formatOptional(signal.getSpeedKmh())
         << "  throttle_percent=" << formatOptional(signal.getThrottlePercent())
         << "  brake_percent=" << formatOptional(signal.getBrakePercent())
         << "  acceleration_g=" << formatOptional(signal.getAccelerationG())
         << "  steering_angle_deg=" << formatOptional(signal.getSteeringAngleDeg())
         << "  motor_rpm=" << formatOptional(signal.getMotorRpm())
         << "  motor_hv_voltage=" << formatOptional(signal.getMotorHvVoltage())
         << "  motor_hv_current=" << formatOptional(signal.getMotorHvCurrent())
         << "  motor_torque_nm=" << formatOptional(signal.getMotorTorqueNm())
         << "  gear_selector=" << gearLabel(signal.getGearSelector())
         << '\n' << std::flush;
}

} // namespace vehicle_sim::pipeline
