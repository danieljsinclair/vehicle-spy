#include "vehicle-sim/domain/VehicleConfig.h"

namespace vehicle_sim::domain {

VehicleConfig::VehicleConfig(
    std::string dbcFilePath,
    std::string vehicleName,
    std::unordered_map<std::string, std::string> signalMappings,
    std::string canBus
) noexcept
    : dbcFilePath(std::move(dbcFilePath))
    , vehicleName(std::move(vehicleName))
    , signalMappings(std::move(signalMappings))
    , canBus(std::move(canBus))
{}

bool VehicleConfig::operator==(
    const VehicleConfig& other
) const noexcept {
    return dbcFilePath == other.dbcFilePath
        && vehicleName == other.vehicleName
        && signalMappings == other.signalMappings
        && canBus == other.canBus;
}

bool VehicleConfig::operator!=(
    const VehicleConfig& other
) const noexcept {
    return !(*this == other);
}

bool VehicleConfig::hasMapping(
    const std::string& signalName
) const noexcept {
    return signalMappings.find(signalName) != signalMappings.end();
}

std::string VehicleConfig::getFieldForSignal(
    const std::string& signalName
) const noexcept {
    auto it = signalMappings.find(signalName);
    return it != signalMappings.end() ? it->second : "";
}

void VehicleConfigRegistry::registerVehicle(
    const std::string& vehicleId,
    VehicleConfig config
) noexcept {
    configs_.insert_or_assign(vehicleId, std::move(config));
}

const VehicleConfig* VehicleConfigRegistry::getConfig(
    const std::string& vehicleId
) const noexcept {
    auto it = configs_.find(vehicleId);
    return it != configs_.end() ? &it->second : nullptr;
}

bool VehicleConfigRegistry::hasConfig(
    const std::string& vehicleId
) const noexcept {
    return configs_.find(vehicleId) != configs_.end();
}

std::vector<std::string> VehicleConfigRegistry::getRegisteredVehicles() const noexcept {
    std::vector<std::string> ids;
    ids.reserve(configs_.size());
    for (const auto& [id, _] : configs_) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace vehicle_sim::domain
