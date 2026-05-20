#include "vehicle-sim/domain/VehicleConfigResolver.h"
#include <stdexcept>
#include <sstream>

namespace vehicle_sim::domain {

VehicleConfigResolver::VehicleConfigResolver(DBCTranslationService& service) noexcept
    : service_(service)
{
}

VehicleConfigResolver::ResolvedConfig VehicleConfigResolver::resolve(const std::string& vehicleType) {
    const auto* config = service_.registry().getConfig(vehicleType);
    if (!config) {
        std::ostringstream oss;
        oss << "Vehicle config not found: " << vehicleType << "\n";
        oss << "Available vehicles: ";
        for (const auto& v : service_.registry().getRegisteredVehicles()) {
            oss << v << " ";
        }
        throw std::runtime_error(oss.str());
    }

    auto protocol = config->isCANProtocol ? VehicleProtocol::CAN : VehicleProtocol::OBD2;

    if (!service_.loadVehicle(vehicleType, protocol)) {
        std::ostringstream oss;
        oss << "Failed to load vehicle config: " << vehicleType;
        throw std::runtime_error(oss.str());
    }

    return {config, protocol};
}

std::vector<std::string> VehicleConfigResolver::availableVehicles() const {
    return service_.registry().getRegisteredVehicles();
}

} // namespace vehicle_sim::domain