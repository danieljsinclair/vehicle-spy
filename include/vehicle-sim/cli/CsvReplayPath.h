#pragma once

#include <string>

namespace vehicle_sim::cli {

// A decoded-telemetry CSV (for CSV replay mode) is distinguished from a raw
// CAN capture by its header: the decoded schema leads with "timestamp_ms".
// Raw CAN captures never do. Used to route --connect file:<csv> to the right
// replay path without a separate flag. Single source of truth for both
// main.cpp and CliOptions.cpp (previously defined identically in each).
bool isDecodedTelemetryCsv(const std::string& path);

} // namespace vehicle_sim::cli
