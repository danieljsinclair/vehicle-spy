#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/VehicleConfig.h"

#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

namespace vehicle_sim::cli {

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    CLI::App app{"Vehicle OBD2 Telemetry Display", "vehicle-sim"};

    app.add_flag("-s,--scan", opts.scan_mode, "Scan for BLE OBD2 adapters");
    app.add_flag("--connect-demo", opts.connect_demo, "Demo mode with live telemetry updates (no hardware)");
    app.add_flag("-m,--simulate", opts.simulate_mode, "Demo mode with mock telemetry");
    app.add_flag("-l,--list", opts.list_signals, "List supported signals for each vehicle");

    app.add_option("-c,--connect", opts.connect_address, "BLE adapter address")
        ->expected(1);
    app.add_option("-v,--vehicle", opts.vehicle_type, "Vehicle type (auto-detected if omitted)")
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

    // Combine all post-parsing logic in final_callback
    app.final_callback([&opts]() {
        // Set connect_mode based on connect_address
        if (!opts.connect_address.empty()) {
            opts.connect_mode = true;
        }

        // --vehicle is required for --connect
        if (opts.connect_mode && opts.vehicle_type.empty()) {
            throw CLI::ValidationError("Vehicle type required with --connect. Supported: tesla, audi_mlb_evo, generic");
        }

        // Validate vehicle type if specified
        if (!opts.vehicle_type.empty()) {
            const std::vector<std::string> supported = {"tesla", "audi_mlb_evo", "generic"};
            bool isValid = std::find(supported.begin(), supported.end(), opts.vehicle_type) != supported.end();
            if (!isValid) {
                throw CLI::ValidationError("Unsupported vehicle type '" + opts.vehicle_type + "'. Supported: tesla, audi_mlb_evo, generic");
            }
        }
    });

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        opts.help_requested = true;
    } catch (const CLI::ParseError& e) {
        opts.error_message = e.what();
    }

    return opts;
}

void printHelp(std::ostream& out, const domain::VehicleConfigRegistry& registry) {
    out << "vehicle-sim - Vehicle OBD2 Telemetry Display\n\n"
        << "USAGE:\n"
        << "  vehicle-sim [OPTIONS]\n\n"
        << "OPTIONS:\n"
        << "  --scan              Scan for BLE OBD2 adapters\n"
        << "  --connect <addr>    Connect to specific BLE adapter address\n"
        << "  --connect-demo      Demo mode with live telemetry (no hardware)\n"
        << "  --vehicle <type>    Vehicle type (required with --connect)\n"
        << "  --list              List supported signals for each vehicle\n"
        << "  --format <fmt>      Output format: json, csv, or plain (default: plain)\n"
        << "  --interval <ms>     Update interval in milliseconds (default: 500)\n"
        << "  --log-csv <file>    Log CSV telemetry to file\n"
        << "  --log-raw <file>    Log raw hex data to file\n"
        << "  --simulate          Demo mode with mock telemetry (no hardware needed)\n"
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
        << "  vehicle-sim --simulate\n"
        << "  vehicle-sim --scan\n"
        << "  vehicle-sim --connect <addr> --vehicle tesla\n"
        << "  vehicle-sim --connect <addr> --vehicle audi_mlb_evo\n"
        << "  vehicle-sim --connect <addr> --vehicle generic\n\n"
        << "NOTES:\n"
        << "  --vehicle is required when using --connect\n"
        << "  tesla and audi_mlb_evo use CAN monitor mode (DBC decoding)\n"
        << "  generic uses standard OBD2 PID polling\n\n"
        << "REQUIREMENTS:\n"
        << "  For real data: Connect a BLE OBD2 adapter to your vehicle's OBD-II port.\n";
}

void printSupportedSignals(std::ostream& out, const domain::VehicleConfigRegistry& registry) {
    auto vehicles = registry.getRegisteredVehicles();
    for (const auto& id : vehicles) {
        const auto* cfg = registry.getConfig(id);
        if (!cfg) continue;

        out << "\n" << cfg->vehicleName << " (" << id << "):\n";
        for (const auto& [signalName, fieldName] : cfg->signalMappings) {
            out << "  " << signalName << " -> " << fieldName << "\n";
        }
        out << "  Protocol: " << (cfg->isCANProtocol ? "CAN (DBC)" : "OBD2 (SAE J1979)") << "\n";
    }
    out << "\n";
}

} // namespace vehicle_sim::cli
