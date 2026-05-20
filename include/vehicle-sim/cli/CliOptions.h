#pragma once

#include <string>
#include <iosfwd>

namespace vehicle_sim::domain {
class VehicleConfigRegistry;
class VehicleConfig;
class DBCTranslationService;
}

namespace vehicle_sim::cli {

// Default telemetry update interval.
// 500ms balances responsiveness with ELM327 BLE bandwidth.
constexpr int DEFAULT_UPDATE_INTERVAL_MS = 500;

// Default output format — human-readable terminal table.
constexpr const char* DEFAULT_FORMAT = "plain";

// Default vehicle type — standard OBD2 PIDs (SAE J1979).
constexpr const char* DEFAULT_VEHICLE_TYPE = "generic";

struct CliOptions {
    bool scan_mode = false;
    bool list_signals = false;
    bool help_requested = false;
    std::string connect_address;
    std::string format = DEFAULT_FORMAT;
    std::string vehicle_type;
    std::string source_type;  // "demo" or "ble"
    int update_interval_ms = DEFAULT_UPDATE_INTERVAL_MS;
    std::string log_csv;
    std::string log_raw;

    // Set on parse error — caller should print and exit(1).
    std::string error_message;
};

// Parse command-line arguments into a structured result.
// Throws CLI::ParseError on --help (caller should catch and call app.exit(e)).
CliOptions parseArgs(int argc, char* argv[]);

// Display help text including registered vehicles from the service.
void printHelp(std::ostream& out, const domain::DBCTranslationService& service);

// List supported signals for each registered vehicle.
void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service);

// Validate CLI options against the registry
// Returns error message if validation fails, empty string if valid
std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service);

} // namespace vehicle_sim::cli
