#pragma once

#include <string>
#include <iosfwd>

namespace vehicle_sim::domain {
class VehicleSignal;
struct VehicleConfig;
}

namespace vehicle_sim::presentation {

// Terminal separator width for telemetry header.
// 70 chars fits standard 80-column terminal with margin.
constexpr int TERMINAL_SEPARATOR_WIDTH = 70;

// Format a single telemetry row for terminal display.
std::string formatTelemetryRow(const domain::VehicleSignal& signal, int count);

// Format the header banner for telemetry display.
std::string formatTelemetryHeader(const domain::VehicleConfig& config);

// Write telemetry row directly to a stream.
void printTelemetryRow(std::ostream& out, const domain::VehicleSignal& signal, int count);

// Write telemetry header directly to a stream.
void printTelemetryHeader(std::ostream& out, const domain::VehicleConfig& config);

} // namespace vehicle_sim::presentation
