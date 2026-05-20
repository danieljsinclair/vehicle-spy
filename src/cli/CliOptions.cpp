#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCTranslationService.h"

#include <CLI/CLI.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <vector>

namespace vehicle_sim::cli {

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    CLI::App app{"Vehicle OBD2 Telemetry Display", "vehicle-sim"};

    app.add_flag("-s,--scan", opts.scan_mode, "Scan for BLE OBD2 adapters");
    app.add_flag("-l,--list", opts.list_signals, "List supported signals for each vehicle");

    app.add_option("--source", opts.source_type, "Telemetry source: demo or ble (required)")
        ->expected(1)
        ->check(CLI::IsMember({"demo", "ble"}));
    app.add_option("-c,--connect", opts.connect_address, "BLE adapter address (required with --source ble)")
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
        << "  --source <type>    Telemetry source: demo or ble (required)\n"
        << "  -c,--connect <addr> BLE adapter address (required with --source ble)\n"
        << "  -v,--vehicle <type> Vehicle type (required)\n"
        << "  -s,--scan           Scan for BLE OBD2 adapters\n"
        << "  -l,--list           List supported signals for each vehicle\n"
        << "  -f,--format <fmt>   Output format: json, csv, or plain (default: plain)\n"
        << "  -i,--interval <ms>  Update interval in milliseconds (default: 500)\n"
        << "  --log-csv <file>    Log CSV telemetry to file\n"
        << "  --log-raw <file>    Log raw hex data to file\n"
        << "  --help              Show this help message\n\n";

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
        << "  vehicle-sim --source demo --vehicle tesla\n"
        << "  vehicle-sim --source ble --connect <addr> --vehicle tesla\n"
        << "  vehicle-sim --scan\n"
        << "  vehicle-sim --list\n\n"
        << "NOTES:\n"
        << "  --source and --vehicle are required for telemetry\n"
        << "  tesla and audi_mlb_evo use CAN monitor mode (DBC decoding)\n"
        << "  generic uses standard OBD2 PID polling\n\n"
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

    // --source is required for telemetry
    if (opts.source_type.empty()) {
        return "--source is required. Use --source demo or --source ble";
    }

    // --connect is required with --source ble
    if (opts.source_type == "ble" && opts.connect_address.empty()) {
        return "--connect address is required with --source ble";
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
