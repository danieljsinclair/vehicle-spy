#pragma once

#include <string>
#include <vector>
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

// Default ESP32 USB serial port for provisioning. Used as a last-resort
// fallback when auto-detect finds no /dev/cu.* match (rare; the device
// enumerates with a predictable name on macOS, and the Makefile's
// ESP32_PORT auto-discovery covers the standard prefixes). The provisioner's
// auto-detect helper is the primary resolver; this is just a backstop.
constexpr const char* ESP32_DEFAULT_USB_PORT = "/dev/cu.usbserial-210";

// WiFi provisioning over the ESP32's AT console (AT command set). Two
// transports reach it, and they are INTERCHANGEABLE: the firmware dispatches
// the same command set on both (AtCommandDispatcher::handleCommand serves
// handleSerialCommand and handleTcpCommand from one registry).
//   * USB serial — local, pre-association (no AUTH required): the bootstrap
//     channel used before any WiFi/credential state exists on the device.
//   * TCP console — the AUTH'd console the firmware serves once the device
//     is associated (port 3333). Useful for a headless device already on
//     the network.
//
// The transport for provisioning is selected by the universal `--connect`
// flag, which folds onto `transport` here (the --connect-{usb,tcp,auto}
// aliases merely compose the same strings). Valid values: "auto"
// (auto-detect the first /dev/cu.* that matches the standard ESP32
// prefixes), "usb:<path>" (explicit path), or "tcp:<host>[:<port>]" (port
// defaults to 3333). Anything else (ble:, demo, file:) is rejected at
// validation time.
struct WifiProvisioningOptions {
    std::string set_wifi_ssid;   // --set-wifi-creds <SSID> <PASS>
    std::string set_wifi_pass;
    bool clear_wifi_creds = false;  // --clear-wifi-creds
    bool reboot_esp32 = false;      // --reboot (send ATREBOOT over the console)
    bool status_requested = false;  // --status (print a [STATE] snapshot from the device)
    std::string transport;          // "auto", "usb:<path>", or "tcp:<host>[:<port>]" (set from --connect; empty = auto-detect)

    // True when a provisioning operation was requested. Used by main.cpp to
    // short-circuit to runProvisioning() before telemetry dispatch.
    [[nodiscard]] bool active() const {
        return !set_wifi_ssid.empty() || clear_wifi_creds || reboot_esp32 ||
               status_requested;
    }
};

// Telemetry connect/transport options.
//
// The `connect_target` field is the canonical transport selector — both for
// telemetry (when no provisioning flag is set) AND for provisioning (folded
// onto `WifiProvisioningOptions::transport` by parseArgs when a provisioning
// flag IS set). The --connect-{usb,tcp,ble,auto} aliases are sugar: parseArgs
// folds them onto the same target so every downstream consumer sees a
// single source of truth.
struct TelemetryOptions {
    std::string connect_target;  // "demo", BLE address/UUID, "file:<path>", "tcp:<ip>:<port>", "usb:<path>", or "auto"
    std::string connect_file;     // `--connect-file <path>` synonym for `--connect file:<path>`
    // --connect-{usb,tcp} short-form values (the tail after the colon). Folded
    // onto connect_target in parseArgs() so every downstream consumer sees a
    // single source of truth.
    std::string connect_usb;
    std::string connect_tcp;
    bool connect_ble = false;  // --connect-ble: marker flag (no value)
    bool connect_auto = false;  // --connect-auto: marker flag
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
    std::string adapter_protocol = "raw";  // --adapter-protocol raw|elm327
};

// Mode/early-exit flags.
struct ModeFlags {
    bool scan_mode = false;
    bool list_signals = false;
    bool discover_mode = false;
    bool help_requested = false;
    bool led_help = false;  // Show StatusLED pattern help
    bool examples_requested = false;  // --examples: show curated usage examples
    std::vector<std::string> help_focus;  // --help --<opt>: filter examples to these topics
    std::string help_text;       // CLI11-derived OPTIONS list (captured on --help)
    std::string examples_text;   // CLI11-derived EXAMPLES (captured on --examples)
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

// Display the OPTIONS list (CLI11-derived). help_text is the rendered OPTIONS
// block captured in parseArgs on --help; it is passed in rather than re-derived
// so printHelp stays a pure presenter. The SUPPORTED VEHICLES block belongs to
// --list (printed via printSupportedSignals), not to --help.
void printHelp(std::ostream& out, const std::string& help_text);

// Display curated usage examples. When focus is non-empty, only examples whose
// topic intersects the focus set are shown (so "--help --connect" produces
// just the --connect examples). topics are the bare flag names without leading
// dashes, e.g. {"connect", "scan"}.
void printExamples(std::ostream& out, const std::string& examples_text,
                   const std::vector<std::string>& focus);

// List supported signals for each registered vehicle.
void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service);

// Display StatusLED pattern reference guide.
void printLedHelp(std::ostream& out);

// Resolve a provisioning transport string to a concrete /dev/cu.* path.
//   "auto"  / ""    -> auto-detect (first matching /dev/cu.{usbserial,SLAB_USBtoUART,wchusbserial}*)
//   "usb:<path>"    -> <path> verbatim
//   anything else   -> "" (validation has rejected it; resolver is defensive)
// Returns the resolved path, or empty if no candidate was found.
//
// This is the USB leg's helper only: the FULL transport resolution (usb: AND
// tcp:, including the single canonical tcp: parse) lives in
// createProvisioningPort() — ProvisioningRunner.h. Do not branch on the
// transport scheme at call sites; go through that factory.
std::string resolveSerialPort(const std::string& transport);

// Validate CLI options against the registry
// Returns error message if validation fails, empty string if valid
std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service);

} // namespace vehicle_sim::cli
