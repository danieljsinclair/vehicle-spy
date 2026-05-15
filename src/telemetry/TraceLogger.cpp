#include "vehicle-sim/telemetry/TraceLogger.h"

#include <sstream>
#include <iomanip>
#include <cmath>

namespace vehicle_sim::telemetry {

TraceLogger::TraceLogger(std::string filePath)
    : file_(filePath)
{
    if (!file_) {
        throw std::runtime_error("Failed to open trace file: " + filePath);
    }
    writeHeader();
}

TraceLogger::~TraceLogger() {
    if (file_.is_open()) {
        file_.close();
    }
}

void TraceLogger::operator()(const domain::VehicleSignal& signal) noexcept {
    if (!file_.is_open()) {
        return;
    }

    writeRow(signal);
}

bool TraceLogger::isValid() const noexcept {
    return file_.is_open();
}

void TraceLogger::writeHeader() {
    file_ << "timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,steering_angle_deg,motor_rpm,motor_hv_voltage,motor_hv_current,gear_selector,motor_torque_nm\n";
    file_.flush();
}

void TraceLogger::writeRow(const domain::VehicleSignal& signal) {
    file_ << signal.getTimestampUtcMs() << ","
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
    file_.flush();
}

std::string TraceLogger::formatOptional(std::optional<double> value) {
    if (!value.has_value()) {
        return "";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << *value;
    return oss.str();
}

std::string TraceLogger::formatOptional(std::optional<std::string> value) {
    if (!value.has_value()) {
        return "";
    }
    return *value;
}

TraceLogger::TraceLogger(TraceLogger&& other) noexcept
    : file_(std::move(other.file_)) {}

TraceLogger& TraceLogger::operator=(TraceLogger&& other) noexcept {
    if (this != &other) {
        if (file_.is_open()) {
            file_.close();
        }
        file_ = std::move(other.file_);
    }
    return *this;
}

}
