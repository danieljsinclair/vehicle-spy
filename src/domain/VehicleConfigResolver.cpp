#include "vehicle-sim/domain/VehicleConfigResolver.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"
#include <sstream>

namespace vehicle_sim::domain {

VehicleConfigResolver::VehicleConfigResolver(DBCTranslationService& service) noexcept
    : service_(service)
{
}

VehicleConfigResolver::ResolvedConfig VehicleConfigResolver::resolve(const std::string& vehicleType) {
    const auto* config = service_.registry().getConfig(vehicleType);
    if (!config) {
        throw VehicleConfigNotFoundException(vehicleType, service_.registry().getRegisteredVehicles());
    }

    auto protocol = config->isCANProtocol ? VehicleProtocol::CAN : VehicleProtocol::OBD2;

    if (!service_.loadVehicle(vehicleType, protocol)) {
        throw DBCLoadException(vehicleType);
    }

    return {config, protocol};
}

std::vector<std::string> VehicleConfigResolver::availableVehicles() const {
    return service_.registry().getRegisteredVehicles();
}

} // namespace vehicle_sim::domain