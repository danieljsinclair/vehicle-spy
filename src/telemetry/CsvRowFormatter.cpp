#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/domain/Gear.h"

#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>

namespace vehicle_sim::telemetry {

namespace {

std::string formatOptional(const std::optional<double>& value) {
    std::string rendered;
    if (value.has_value()) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << *value;
        rendered = oss.str();
    }
    return rendered;
}

std::string formatGear(const std::optional<std::int32_t>& gear) {
    std::string rendered;
    if (gear.has_value()) {
        rendered = domain::Gear::labelOr(*gear, std::to_string(*gear));
    }
    return rendered;
}

// dbc_signal_count: how many of the 10 translated signal columns are
// populated. timestamp and vehicle_id are NOT counted — only DBC-translated
// signals, matching the schema's documented meaning.
int countPopulated(const domain::VehicleSignal& signal) {
    int populated = 0;
    if (signal.getThrottlePercent().has_value())  { ++populated; }
    if (signal.getSpeedKmh().has_value())         { ++populated; }
    if (signal.getAccelerationG().has_value())    { ++populated; }
    if (signal.getBrakePercent().has_value())     { ++populated; }
    if (signal.getSteeringAngleDeg().has_value()) { ++populated; }
    if (signal.getMotorRpm().has_value())         { ++populated; }
    if (signal.getMotorHvVoltage().has_value())   { ++populated; }
    if (signal.getMotorHvCurrent().has_value())   { ++populated; }
    if (signal.getMotorTorqueNm().has_value())    { ++populated; }
    if (signal.getGearSelector().has_value())     { ++populated; }
    return populated;
}

} // namespace

std::string csvHeaderLine() {
    return "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_percent,"
           "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
           "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count";
}

std::string csvRowLine(const domain::VehicleSignal& signal,
                       const std::string& vehicleId) {
    std::ostringstream row;
    row << signal.getTimestampUtcMs() << ","
        << vehicleId << ","
        << formatOptional(signal.getSpeedKmh()) << ","
        << formatOptional(signal.getThrottlePercent()) << ","
        << formatOptional(signal.getBrakePercent()) << ","
        << formatOptional(signal.getAccelerationG()) << ","
        << formatOptional(signal.getSteeringAngleDeg()) << ","
        << formatOptional(signal.getMotorRpm()) << ","
        << formatOptional(signal.getMotorHvVoltage()) << ","
        << formatOptional(signal.getMotorHvCurrent()) << ","
        << formatOptional(signal.getMotorTorqueNm()) << ","
        << formatGear(signal.getGearSelector()) << ","
        << countPopulated(signal);
    return row.str();
}

} // namespace vehicle_sim::telemetry
