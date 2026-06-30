#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCTranslationService.h"

#include <CLI/CLI.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace vehicle_sim::cli {

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    CLI::App app{"Vehicle OBD2 Telemetry Display", "vehicle-sim"};

    app.add_flag("-s,--scan", opts.scan_mode, "Scan for BLE OBD2 adapters");
    app.add_flag("-l,--list", opts.list_signals, "List supported signals for each vehicle");
    app.add_flag("--discover", opts.discover_mode, "Discover ESP32 devices on the network via UDP broadcast");

    app.add_option("-c,--connect", opts.connect_target,
                   "Connect target: 'demo', 'file:<path>', 'tcp:<ip>:<port>', 'usb:<path>', "
                   "'auto' (auto-discover ESP32), or BLE adapter address")
        ->expected(1);
    app.add_option("-v,--vehicle", opts.vehicle_type, "Vehicle type (required)")
        ->expected(1);
    app.add_option("-f,--format", opts.format, "Output format: json, csv, or plain")
        ->expected(1)
        ->capture_default_str();
    app.add_option("-i,--interval", opts.update_interval_ms, "Update interval in milliseconds")
        ->expected(1)
        ->capture_default_str()
        ->check(CLI::PositiveNumber);
    // Canonical logging flag — base path. Phase 1 file replay writes only
    // <base>.csv; the raw stream is not duplicated (input file is source of
    // truth). Later phases write <base>.raw.txt for live transports.
    app.add_option("--log", opts.log_base,
                   "Log base path: writes <base>.csv (decoded). For live transports "
                   "also writes <base>.raw.txt (raw capture)")
        ->expected(1);
    app.add_option("--adapter-protocol", opts.adapter_protocol,
                   "Adapter protocol: 'raw' (default) or 'elm327'. Default table "
                   "applies when omitted: demo/file/tcp/usb→raw, ble→elm327")
        ->expected(1)
        ->capture_default_str();
    // Deprecated aliases — mapped onto --log semantics in main.cpp.
    app.add_option("--log-csv", opts.log_csv,
                   "(deprecated, use --log <base>) Log decoded CSV telemetry to file")
        ->expected(1);
    app.add_option("--log-raw", opts.log_raw,
                   "(deprecated, use --log <base>) Log raw hex/TWAI data to file")
        ->expected(1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        opts.help_requested = true;
    } catch (const CLI::ParseError& e) {
        opts.error_message = e.what();
    }

    return opts;
}

void printHelp(std::ostream& out, const domain::DBCTranslationService& service) {
    auto& registry = service.registry();
    out << "vehicle-sim - Vehicle OBD2 Telemetry Display\n\n"
        << "USAGE:\n"
        << "  vehicle-sim [OPTIONS]\n\n"
        << "OPTIONS:\n"
        << "  -c,--connect <target> Connect target: 'demo', 'file:<path>', 'tcp:<ip>:<port>',\n"
        << "                        'usb:<path>', 'auto' (auto-discover ESP32), or BLE adapter address\n"
        << "  -v,--vehicle <type>   Vehicle type (required, or 'auto' to detect)\n"
        << "  -s,--scan             Scan for BLE OBD2 adapters\n"
        << "  -l,--list             List supported signals for each vehicle\n"
        << "      --discover        Discover ESP32 devices on the network\n"
        << "  -f,--format <fmt>     Output format: json, csv, or plain (default: plain)\n"
        << "  -i,--interval <ms>    Update interval in milliseconds (default: 500)\n"
        << "  --log <base>          Log base path: writes <base>.csv (decoded); live\n"
        << "                        transports also write <base>.raw.txt (raw capture)\n"
        << "  --adapter-protocol <p> Adapter protocol: raw (default) or elm327\n"
        << "  --log-csv <file>      (deprecated, use --log) Log decoded CSV to file\n"
        << "  --log-raw <file>      (deprecated, use --log) Log raw hex to file\n"
        << "  --help                Show this help message\n\n";

    if (auto vehicles = registry.getRegisteredVehicles(); !vehicles.empty()) {
        out << "SUPPORTED VEHICLES:\n";
        for (const auto& id : vehicles) {
            const auto* cfg = registry.getConfig(id);
            if (cfg) {
                out << "  " << id << "  (" << cfg->vehicleName << ")\n";
            }
        }
        out << "\n";
    }

    out << "EXAMPLES:\n"
        << "  vehicle-sim --connect demo --vehicle tesla\n"
        << "  vehicle-sim --discover\n"
        << "  vehicle-sim --connect auto --vehicle tesla\n"
        << "  vehicle-sim --connect file:capture.csv --vehicle tesla --log-csv decoded.csv\n"
        << "  vehicle-sim --connect tcp:192.168.4.1:3333 --vehicle tesla --log-raw x.raw --log-csv x.csv\n"
        << "  vehicle-sim --connect usb:/dev/cu.usbserial-110 --vehicle tesla --log captures/SecondDrive\n"
        << "  vehicle-sim --connect tcp:192.168.4.1 --vehicle tesla\n"
        << "  vehicle-sim --connect <addr> --vehicle tesla\n"
        << "  vehicle-sim --connect <addr> --vehicle auto\n"
        << "  vehicle-sim --scan\n"
        << "  vehicle-sim --list\n\n"
        << "NOTES:\n"
        << "  --connect and --vehicle are required for telemetry\n"
        << "  'auto' discovers an ESP32 on the UDP discovery port (3335) and connects automatically\n"
        << "  file:<path> replays a captured raw CAN CSV\n"
        << "  tcp:<ip>:<port> streams live CAN frames from an ESP32 CAN-bridge over WiFi\n"
        << "    (port defaults to 3333 when omitted; e.g. tcp:192.168.4.1)\n"
        << "  usb:<path> streams live CAN frames from an ESP32 CAN-bridge over USB serial\n"
        << "    (for example /dev/cu.usbserial-110 at 115200 8N1)\n"
        << "  tesla and audi_mlb_evo use CAN monitor mode (DBC decoding)\n"
        << "  generic uses standard OBD2 PID polling\n"
        << "  CAN monitor mode is read-only (ATCSM1: no ACK bits on bus)\n\n"
        << "REQUIREMENTS:\n"
        << "  For real data: Connect a BLE OBD2 adapter to your vehicle's OBD-II port,\n"
        << "  connect over WiFi to an ESP32 CAN-bridge (tcp:<ip>:3333), or connect\n"
        << "  directly by USB serial (usb:/dev/cu.usbserial-110).\n";
}

void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service) {
    auto vehicles = service.registry().getRegisteredVehicles();
    for (const auto& id : vehicles) {
        const auto* cfg = service.registry().getConfig(id);
        if (!cfg) continue;

        out << "\n" << cfg->vehicleName << " (" << id << "):\n";
        for (const auto& [signalName, fieldName] : cfg->signalMappings) {
            out << "  " << signalName << " -> " << fieldName << "\n";
        }
        out << "  Protocol: " << (cfg->isCANProtocol ? "CAN (DBC)" : "OBD2 (SAE J1979)") << "\n";
    }
    out << "\n";
}

std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service) {
    auto& registry = service.registry();

    // Skip validation for scan, list, help, discover
    if (opts.scan_mode || opts.list_signals || opts.help_requested || opts.discover_mode) {
        return "";
    }

    // --adapter-protocol must be a known value.
    if (!opts.adapter_protocol.empty() &&
        opts.adapter_protocol != "raw" &&
        opts.adapter_protocol != "elm327" &&
        opts.adapter_protocol != "default") {
        std::ostringstream oss;
        oss << "Unknown --adapter-protocol '" << opts.adapter_protocol
            << "'. Supported: raw, elm327";
        return oss.str();
    }

    // --connect is required for telemetry
    if (opts.connect_target.empty()) {
        return "--connect is required. Use --connect demo, --connect auto, or --connect <address>";
    }

    // --vehicle is required
    if (opts.vehicle_type.empty()) {
        std::ostringstream oss;
        oss << "--vehicle is required. Available: ";
        auto vehicles = registry.getRegisteredVehicles();
        for (const auto& v : vehicles) {
            oss << v << " ";
        }
        return oss.str();
    }

    // "auto" is valid — resolved at runtime via UDP discovery
    if (opts.vehicle_type == "auto") {
        if (!opts.isBLE()) {
            return "--vehicle auto requires a BLE connection. Use --connect <address> --vehicle auto";
        }
        return "";
    }

    // Validate vehicle type against registry
    if (!registry.hasConfig(opts.vehicle_type)) {
        std::ostringstream oss;
        oss << "Unsupported vehicle type '" << opts.vehicle_type << "'. Available: ";
        auto vehicles = registry.getRegisteredVehicles();
        for (const auto& v : vehicles) {
            oss << v << " ";
        }
        return oss.str();
    }

    return "";
}

} // namespace vehicle_sim::cli
