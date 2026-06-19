#include "vehicle-sim/telemetry/CsvStdoutSink.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace vehicle_sim::telemetry {

CsvStdoutSink::CsvStdoutSink(std::ostream& out)
    : out_(&out)
{
    writeHeader();
}

void CsvStdoutSink::operator()(const domain::VehicleSignal& signal) noexcept {
    if (!out_) {
        return;
    }
    writeRow(signal);
}

void CsvStdoutSink::writeHeader() {
    *out_ << "timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,steering_angle_deg,motor_rpm,motor_hv_voltage,motor_hv_current,gear_selector,motor_torque_nm\n";
    out_->flush();
}

void CsvStdoutSink::writeRow(const domain::VehicleSignal& signal) {
    *out_ << signal.getTimestampUtcMs() << ","
          << formatOptional(signal.getThrottlePercent()) << ","
          << formatOptional(signal.getSpeedKmh()) << ","
          << formatOptional(signal.getAccelerationG()) << ","
          << formatOptional(signal.getBrakePercent()) << ","
          << formatOptional(signal.getSteeringAngleDeg()) << ","
          << formatOptional(signal.getMotorRpm()) << ","
          << formatOptional(signal.getMotorHvVoltage()) << ","
          << formatOptional(signal.getMotorHvCurrent()) << ","
          << formatOptional(signal.getGearSelector()) << ","
          << formatOptional(signal.getMotorTorqueNm())
          << "\n";
    out_->flush();
}

std::string CsvStdoutSink::formatOptional(std::optional<double> value) {
    if (!value.has_value()) {
        return "";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << *value;
    return oss.str();
}

std::string CsvStdoutSink::formatOptional(std::optional<std::int32_t> value) {
    if (!value.has_value()) {
        return "";
    }
    return std::to_string(*value);
}

void NullCsvStdoutSink::operator()(const domain::VehicleSignal& /*signal*/) noexcept {
    // Intentionally no-op.
}

std::unique_ptr<ICsvStdoutSink> createStdoutSink(bool enabled, std::ostream& out) {
    if (enabled) {
        return std::make_unique<CsvStdoutSink>(out);
    }
    return std::make_unique<NullCsvStdoutSink>();
}

} // namespace vehicle_sim::telemetry
