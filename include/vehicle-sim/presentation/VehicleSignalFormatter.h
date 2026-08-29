#pragma once

#include <string>
#include <iosfwd>

namespace vehicle_sim::domain {
class VehicleSignal;
struct VehicleConfig;
struct VehicleDetectionResult;
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

// One-line summary of the passive vehicle detector's findings:
//   "Frames: 12 | CAN IDs: 0x0118 0x0257 | tesla (high)"
// CAN IDs are listed in ascending order (deterministic output; the detector
// collects them in an unordered set). Returns "" when no frames were
// observed yet — there is nothing to report.
std::string formatDetectionSummary(const domain::VehicleDetectionResult& result);

} // namespace vehicle_sim::presentation
