#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/domain/VehicleConfigResolver.h"
#include <iostream>

namespace vehicle_sim::cli {

void printBanner() {
    std::cout << "vehicle-sim v1.0.0 - Vehicle OBD2 Telemetry Display\n";
}

bool handleEarlyExit(
    const CliOptions& opts,
    const domain::DBCTranslationService& translationService
) {
    if (!opts.error_message.empty()) {
        return true;
    }

    if (opts.help_requested) {
        printHelp(std::cout, translationService);
        return true;
    }

    if (opts.list_signals) {
        printSupportedSignals(std::cout, translationService);
        return true;
    }

    if (opts.led_diag) {
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