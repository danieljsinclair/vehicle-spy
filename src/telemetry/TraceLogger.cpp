#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <sstream>
#include <iomanip>
#include <cmath>

namespace vehicle_sim::telemetry {

TraceLogger::TraceLogger(const std::string& filePath, std::string vehicleId)
    : file_(filePath)
    , vehicleId_(std::move(vehicleId))
{
    if (!file_) {
        throw domain::TelemetryFileException(filePath);
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
    file_ << "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_percent,acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count\n";
    file_.flush();
}

void TraceLogger::writeRow(const domain::VehicleSignal& signal) {
    const auto& gear = signal.getGearSelector();

    // dbc_signal_count: count of POPULATED (non-nullopt) fields among the 10
    // translated signal columns (throttle, speed, accel, brake, steering, rpm,
    // hv_voltage, hv_current, torque, gear). timestamp and vehicle_id are NOT
    // counted — only DBC-translated signals.
    int populatedCount = 0;
    if (signal.getThrottlePercent().has_value())  ++populatedCount;
    if (signal.getSpeedKmh().has_value())         ++populatedCount;
    if (signal.getAccelerationG().has_value())    ++populatedCount;
    if (signal.getBrakePercent().has_value())     ++populatedCount;
    if (signal.getSteeringAngleDeg().has_value()) ++populatedCount;
    if (signal.getMotorRpm().has_value())         ++populatedCount;
    if (signal.getMotorHvVoltage().has_value())   ++populatedCount;
    if (signal.getMotorHvCurrent().has_value())   ++populatedCount;
    if (signal.getMotorTorqueNm().has_value())    ++populatedCount;
    if (gear.has_value())                         ++populatedCount;

    file_ << signal.getTimestampUtcMs() << ","
          << vehicleId_ << ","
          << formatOptional(signal.getSpeedKmh()) << ","
          << formatOptional(signal.getThrottlePercent()) << ","
          << formatOptional(signal.getBrakePercent()) << ","
          << formatOptional(signal.getAccelerationG()) << ","
          << formatOptional(signal.getSteeringAngleDeg()) << ","
          << formatOptional(signal.getMotorRpm()) << ","
          << formatOptional(signal.getMotorHvVoltage()) << ","
          << formatOptional(signal.getMotorHvCurrent()) << ","
          << formatOptional(signal.getMotorTorqueNm()) << ","
          << (gear.has_value() ? domain::Gear::labelOr(*gear, std::to_string(*gear)) : "") << ","
          << populatedCount
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

std::string TraceLogger::formatOptional(std::optional<std::int32_t> value) {
    if (!value.has_value()) {
        return "";
    }
    return std::to_string(*value);
}

TraceLogger::TraceLogger(TraceLogger&& other) noexcept
    : file_(std::move(other.file_))
    , vehicleId_(std::move(other.vehicleId_)) {}

TraceLogger& TraceLogger::operator=(TraceLogger&& other) noexcept {
    if (this != &other) {
        if (file_.is_open()) {
            file_.close();
        }
        file_ = std::move(other.file_);
        vehicleId_ = std::move(other.vehicleId_);
    }
    return *this;
}

}
