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

// Default ESP32 USB serial port for provisioning. Mirrors the Makefile's
// ESP32_PORT auto-detection fallback (/dev/cu.usbserial* et al.); the
// --port flag and the ESP32_PORT env var override this.
constexpr const char* ESP32_DEFAULT_USB_PORT = "/dev/cu.usbserial-210";

// WiFi provisioning over the ESP32 USB serial console (AT command set).
// Local, pre-association (no AUTH required): USB serial is the bootstrap
// channel used before any WiFi/credential state exists on the device.
struct WifiProvisioningOptions {
    std::string set_wifi_ssid;   // --set-wifi-creds <SSID> <PASS>
    std::string set_wifi_pass;
    bool clear_wifi_creds = false;  // --clear-wifi-creds
    bool reboot_esp32 = false;      // --reboot (send ATREBOOT over USB)
    std::string usb_port = ESP32_DEFAULT_USB_PORT;  // --port <path>

    // True when a USB-serial provisioning operation was requested. Used by
    // main.cpp to short-circuit to runProvisioning() before telemetry dispatch.
    [[nodiscard]] bool active() const {
        return !set_wifi_ssid.empty() || clear_wifi_creds || reboot_esp32;
    }
};

// Telemetry connect/transport options.
struct TelemetryOptions {
    std::string connect_target;  // "demo", BLE address/UUID, "file:<path>", "tcp:<ip>:<port>", "usb:<path>", or "auto"
    std::string connect_file;     // `--connect-file <path>` synonym for `--connect file:<path>`
    std::string format = DEFAULT_FORMAT;
    std::string vehicle_type;
    int update_interval_ms = DEFAULT_UPDATE_INTERVAL_MS;
    double start_from_s = -1.0;  // Replay-only: skip rows whose recorded timestamp is before this many seconds. Negative/unset means "no skip". Mirrors engine-sim-cli's setStartFromS. Applied only to file (replay) replay, not live feeds.
    bool interactive_mode = false;  // Keyboard-driven CSV emission (bench testing)
    bool stdout_csv = false;  // Emit decoded CSV rows to stdout (same schema as <base>.csv). When set, human-readable progress/banners move to stderr so stdout stays a clean, pipeable CSV stream.
};

// Logging output options.
struct LoggingOptions {
    std::string log_base;        // --log <base>: canonical decoded-CSV base ("<base>.csv")
    std::string log_csv;         // Deprecated alias (kept for migration). Mapped onto log_base in main.cpp.
    std::string log_raw;         // Deprecated alias (kept for migration). Mapped onto log_base in main.cpp.
    std::string adapter_protocol = "raw";  // --adapter-protocol raw|elm327
};

// Mode/early-exit flags.
struct ModeFlags {
    bool scan_mode = false;
    bool list_signals = false;
    bool discover_mode = false;
    bool help_requested = false;
    bool led_help = false;  // Show StatusLED pattern help
    std::string help_text;
};

struct CliOptions {
    ModeFlags mode;
    WifiProvisioningOptions wifi;
    TelemetryOptions telemetry;
    LoggingOptions logging;

    // Set on parse error — caller should print and exit(1). Process-wide parse
    // failure, so it stays top-level rather than living in any group.
    std::string error_message;

    [[nodiscard]] bool isDemo() const { return telemetry.connect_target == "demo"; }
    [[nodiscard]] bool isFile() const noexcept { return telemetry.connect_target.rfind("file:", 0) == 0; }
    [[nodiscard]] bool isTcp() const noexcept { return telemetry.connect_target.rfind("tcp:", 0) == 0; }
    [[nodiscard]] bool isUsb() const noexcept { return telemetry.connect_target.rfind("usb:", 0) == 0; }
    [[nodiscard]] bool isAuto() const { return telemetry.connect_target == "auto"; }
    [[nodiscard]] bool isBLE() const {
        return !telemetry.connect_target.empty() && !isDemo() && !isFile() &&
               !isTcp() && !isUsb() && !isAuto();
    }

    // True when a USB-serial provisioning operation was requested. Used by
    // main.cpp to short-circuit to runProvisioning() before telemetry dispatch.
    [[nodiscard]] bool isProvisioning() const { return wifi.active(); }
};

// Parse command-line arguments into a structured result.
CliOptions parseArgs(int argc, char* argv[]);

// Display help text including registered vehicles from the service.
// help_text is the CLI11-derived help (auto OPTIONS + footer) captured in
// parseArgs on --help; it is passed in rather than re-derived so printHelp
// stays a pure presenter.
void printHelp(std::ostream& out, const domain::DBCTranslationService& service,
               const std::string& help_text);

// List supported signals for each registered vehicle.
void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service);

// Display StatusLED pattern reference guide.
void printLedHelp(std::ostream& out);

// Validate CLI options against the registry
// Returns error message if validation fails, empty string if valid
std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service);

} // namespace vehicle_sim::cli
