#pragma once

#include <string>
#include <iosfwd>

namespace vehicle_sim::domain {
class VehicleConfigRegistry;
struct VehicleConfig;
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
    bool discover_mode = false;
    bool help_requested = false;
    std::string connect_target;  // "demo", BLE address/UUID, "file:<path>", "tcp:<ip>:<port>", "usb:<path>", or "auto"
    std::string format = DEFAULT_FORMAT;
    std::string vehicle_type;
    int update_interval_ms = DEFAULT_UPDATE_INTERVAL_MS;
    std::string log_base;        // --log <base>: canonical decoded-CSV base ("<base>.csv")
    std::string adapter_protocol = "raw";  // --adapter-protocol raw|elm327
    // Deprecated aliases (kept for migration). Empty unless the caller used the
    // old flag; mapped onto log_base / raw-output semantics in main.cpp.
    std::string log_csv;
    std::string log_raw;

    // Set on parse error — caller should print and exit(1).
    std::string error_message;

    [[nodiscard]] bool isDemo() const { return connect_target == "demo"; }
    [[nodiscard]] bool isFile() const noexcept { return connect_target.rfind("file:", 0) == 0; }
    [[nodiscard]] bool isTcp() const noexcept { return connect_target.rfind("tcp:", 0) == 0; }
    [[nodiscard]] bool isUsb() const noexcept { return connect_target.rfind("usb:", 0) == 0; }
    [[nodiscard]] bool isAuto() const { return connect_target == "auto"; }
    [[nodiscard]] bool isBLE() const {
        return !connect_target.empty() && !isDemo() && !isFile() && !isTcp() && !isUsb() && !isAuto();
    }
};

// Parse command-line arguments into a structured result.
CliOptions parseArgs(int argc, char* argv[]);

// Display help text including registered vehicles from the service.
void printHelp(std::ostream& out, const domain::DBCTranslationService& service);

// List supported signals for each registered vehicle.
void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service);

// Validate CLI options against the registry
// Returns error message if validation fails, empty string if valid
std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service);

} // namespace vehicle_sim::cli
