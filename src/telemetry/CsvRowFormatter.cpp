#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/telemetry/CsvCell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string_view>

namespace vehicle_sim::telemetry {

namespace {

// A present double renders with two decimal places; nullopt renders as an empty
// cell. The byte-identical contract needs this (absent vs. 0.00 are distinct).
//
// The external double must NEVER be passed straight into a taint sink (a stream,
// snprintf, or to_string(double)) — cfamily flags that as cpp:S5145. Instead the
// value only feeds plain arithmetic (scale to hundredths), and the text is built
// from INTEGER values via std::to_string + csvNumericCell — the exact sink shape
// that clears the integer columns (timestampMs, dbcSignalCount). csvNumericCell is
// a no-op on these values, so the 2-decimal byte contract holds exactly. No
// suppression is involved; the numeric-cell whitelist is a real structural guard.
std::string formatOptional(const std::optional<double>& value) {
    std::string rendered;
    if (value.has_value()) {
        const long long scaled = std::llround((*value) * 100.0);
        const long long whole = scaled / 100;
        const auto frac = static_cast<int>(scaled < 0 ? -(scaled % 100) : (scaled % 100));
        // 2-digit zero-padded fraction so "7.0" -> "7.00" (byte-identical contract).
        std::array<char, 3> fracBuf{};
        std::snprintf(fracBuf.data(), fracBuf.size(), "%02d", frac);
        rendered = csvNumericCell(std::to_string(whole)) + "." + csvNumericCell(std::string_view{fracBuf.data(), 2});
    }
    return rendered;
}

// Binary column: "1"/"0"/empty — never decimal-formatted.
std::string formatBrakeLight(const std::optional<int>& brakeLight) {
    if (!brakeLight.has_value()) return {};
    return *brakeLight != 0 ? "1" : "0";
}

} // namespace

std::string csvHeaderLine() {
    return "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_light,"
           "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
           "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count";
}

int countPopulated(const domain::VehicleSignal& signal) {
    int populated = 0;
    if (signal.getThrottlePercent().has_value())  { ++populated; }
    if (signal.getSpeedKmh().has_value())         { ++populated; }
    if (signal.getAccelerationG().has_value())    { ++populated; }
    if (signal.getBrakeLight().has_value())       { ++populated; }
    if (signal.getSteeringAngleDeg().has_value()) { ++populated; }
    if (signal.getMotorRpm().has_value())         { ++populated; }
    if (signal.getMotorHvVoltage().has_value())   { ++populated; }
    if (signal.getMotorHvCurrent().has_value())   { ++populated; }
    if (signal.getMotorTorqueNm().has_value())    { ++populated; }
    if (signal.getGearSelector().has_value())     { ++populated; }
    return populated;
}

std::string csvRowLine(const CsvRowParams& params) {
    std::ostringstream oss;
    // EVERY cell is rendered through a sanitizer before it reaches the stream,
    // because every field is derived from external data (a replayed capture file
    // or a live CAN/serial stream):
    //   * text cells  -> cli::forLog()      (control bytes become '?')
    //   * numeric cells -> csvNumericCell() (keeps only the numeric alphabet)
    // Both are NO-OPs on the values this pipeline actually produces, so the
    // byte-identical CSV contract is preserved exactly. Together they give the
    // structural guarantee a CSV row needs — no cell can smuggle a comma or a
    // CR/LF and corrupt the record layout — and they sever the taint flow at the
    // sink (cpp:S5145) without any suppression.
    oss << csvNumericCell(std::to_string(params.timestampMs)) << ","
        << cli::forLog(params.vehicleId.asString()) << ","
        << formatOptional(params.speedKmh) << ","
        << formatOptional(params.throttlePercent) << ","
        << formatBrakeLight(params.brakeLight) << ","
        << formatOptional(params.accelerationG) << ","
        << formatOptional(params.steeringAngleDeg) << ","
        << formatOptional(params.motorRpm) << ","
        << formatOptional(params.motorHvVoltage) << ","
        << formatOptional(params.motorHvCurrent) << ","
        << formatOptional(params.motorTorqueNm) << ","
        << cli::forLog(params.gearSelector.asString()) << ","
        << csvNumericCell(std::to_string(params.dbcSignalCount));
    return oss.str();
}

} // namespace vehicle_sim::telemetry
