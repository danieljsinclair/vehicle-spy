#pragma once

#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include <string>
#include <vector>

namespace vehicle_sim::domain {

/**
 * Resolves vehicle configuration for CLI execution
 *
 * Centralizes vehicle type validation, config lookup, protocol determination,
 * and DBC loading. Violates SRP if these are scattered in main().
 *
 * DI: Receives DBCTranslationService and registry via constructor.
 */
class VehicleConfigResolver {
public:
    VehicleConfigResolver(DBCTranslationService& service) noexcept;

    /**
     * Resolve vehicle configuration for the given type
     *
     * @param vehicleType Vehicle type identifier (e.g., "tesla", "generic")
     * @return Tuple of (config pointer, protocol)
     * @throws std::runtime_error if vehicle type not found or DBC loading fails
     */
    struct ResolvedConfig {
        const VehicleConfig* config;
        VehicleProtocol protocol;
    };

    [[nodiscard]] ResolvedConfig resolve(const std::string& vehicleType);

    /**
     * Get available vehicle IDs
     */
    [[nodiscard]] std::vector<std::string> availableVehicles() const;

private:
    DBCTranslationService& service_;
};

} // namespace vehicle_sim::domain