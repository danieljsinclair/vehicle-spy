#pragma once

#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include <string>

namespace vehicle_sim::cli {

/**
 * Print application banner
 */
void printBanner();

/**
 * Handle early exit conditions (help, list, parse errors)
 *
 * @param opts Parsed CLI options
 * @param translationService DBC translation service (for help/list)
 * @return true if should early exit, false if continue to telemetry
 */
[[nodiscard]] bool handleEarlyExit(
    const CliOptions& opts,
    const domain::DBCTranslationService& translationService
);

/**
 * Vehicle context for telemetry execution
 */
struct VehicleContext {
    const domain::VehicleConfig* config;
    domain::VehicleProtocol protocol;
    std::string vehicleType;
};

/**
 * Resolve vehicle configuration for telemetry
 *
 * @param vehicleType Vehicle type identifier
 * @param translationService DBC translation service
 * @return Resolved vehicle context
 * @throws std::runtime_error if resolution fails
 */
[[nodiscard]] VehicleContext resolveVehicleContext(
    const std::string& vehicleType,
    domain::DBCTranslationService& translationService
);

} // namespace vehicle_sim::cli