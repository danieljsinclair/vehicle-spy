#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/domain/VehicleConfigResolver.h"
#include <iostream>

namespace vehicle_sim::cli {

void printBanner(std::ostream& out) {
    out << "vehicle-sim v1.0.0 - Vehicle OBD2 Telemetry Display\n";
}

void printBanner() {
    printBanner(std::cout);
}

bool handleEarlyExit(
    const CliOptions& opts,
    const domain::DBCTranslationService& translationService
) {
    if (!opts.error_message.empty()) {
        return true;
    }

    if (opts.mode.examples_requested) {
        // --examples always wins over --help when both are given; it is the
        // curated examples block, with focus filtering applied if --help
        // was also passed with a topic (e.g. `--help --connect --examples`).
        printExamples(std::cout, opts.mode.examples_text, opts.mode.help_focus);
        return true;
    }

    if (opts.mode.help_requested) {
        // --help shows ONLY the OPTIONS list. EXAMPLES belongs to --examples.
        // SUPPORTED VEHICLES belongs to --list (the only owner of that block).
        printHelp(std::cout, opts.mode.help_text);
        // When --help is given with a focus token, also surface the matching
        // EXAMPLES block inline so `--help --connect` is one screen.
        if (!opts.mode.help_focus.empty() && !opts.mode.examples_text.empty()) {
            printExamples(std::cout, opts.mode.examples_text, opts.mode.help_focus);
        }
        return true;
    }

    if (opts.mode.list_signals) {
        printSupportedSignals(std::cout, translationService);
        return true;
    }

    if (opts.mode.led_help) {
        printLedHelp(std::cout);
        return true;
    }

    return false;
}

VehicleContext resolveVehicleContext(
    const std::string& vehicleType,
    domain::DBCTranslationService& translationService
) {
    domain::VehicleConfigResolver resolver(translationService);
    auto [config, protocol] = resolver.resolve(vehicleType);
    return {config, protocol, vehicleType};
}

} // namespace vehicle_sim::cli