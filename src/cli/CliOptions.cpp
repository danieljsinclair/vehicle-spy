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

    app.add_option("-c,--connect", opts.connect_target, "Connect target: 'demo' for simulation, or BLE adapter address")
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
    app.add_option("--log-csv", opts.log_csv, "Log CSV telemetry to file")
        ->expected(1);
    app.add_option("--log-raw", opts.log_raw, "Log raw hex data to file")
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
        << "  -c,--connect <target> Connect target: 'demo' or BLE adapter address (required)\n"
        << "  -v,--vehicle <type>   Vehicle type (required, or 'auto' to detect)\n"
        << "  -s,--scan             Scan for BLE OBD2 adapters\n"
        << "  -l,--list             List supported signals for each vehicle\n"
        << "  -f,--format <fmt>     Output format: json, csv, or plain (default: plain)\n"
        << "  -i,--interval <ms>    Update interval in milliseconds (default: 500)\n"
        << "  --log-csv <file>      Log CSV telemetry to file\n"
        << "  --log-raw <file>      Log raw hex data to file\n"
        << "  --help                Show this help message\n\n";

    auto vehicles = registry.getRegisteredVehicles();
    if (!vehicles.empty()) {
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
        << "  vehicle-sim --connect <addr> --vehicle tesla\n"
        << "  vehicle-sim --connect <addr> --vehicle auto\n"
        << "  vehicle-sim --scan\n"
        << "  vehicle-sim --list\n\n"
        << "NOTES:\n"
        << "  --connect and --vehicle are required for telemetry\n"
        << "  tesla and audi_mlb_evo use CAN monitor mode (DBC decoding)\n"
        << "  generic uses standard OBD2 PID polling\n"
        << "  CAN monitor mode is read-only (ATCSM1: no ACK bits on bus)\n\n"
        << "REQUIREMENTS:\n"
        << "  For real data: Connect a BLE OBD2 adapter to your vehicle's OBD-II port.\n";
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

    // Skip validation for scan, list, help
    if (opts.scan_mode || opts.list_signals || opts.help_requested) {
        return "";
    }

    // --connect is required for telemetry
    if (opts.connect_target.empty()) {
        return "--connect is required. Use --connect demo or --connect <address>";
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

    // "auto" is valid — resolved at runtime via VIN detection (BLE only)
    if (opts.vehicle_type == "auto") {
        if (opts.isDemo()) {
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
