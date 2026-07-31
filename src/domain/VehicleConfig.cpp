#include "vehicle-sim/domain/VehicleConfig.h"

namespace vehicle_sim::domain {

VehicleConfig::VehicleConfig(
    std::string dbcFilePath,
    std::string dbcBundleFileName,
    std::string vehicleName,
    std::unordered_map<std::string, std::string> signalMappings,
    std::string canBus,
    bool isCANProtocol
) noexcept
    : dbcFilePath(std::move(dbcFilePath))
    , dbcBundleFileName(std::move(dbcBundleFileName))
    , vehicleName(std::move(vehicleName))
    , signalMappings(std::move(signalMappings))
    , canBus(std::move(canBus))
    , isCANProtocol(isCANProtocol)
{}

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

std::string VehicleConfigRegistry::getDbcBundleFileName(
    const std::string& vehicleId
) const noexcept {
    const auto* config = getConfig(vehicleId);
    return config ? config->dbcBundleFileName : "";
}

std::vector<VehicleConfigRegistry::VehicleOption> VehicleConfigRegistry::getVehicleOptions() const noexcept {
    std::vector<VehicleOption> options;
    options.reserve(configs_.size());
    for (const auto& [id, config] : configs_) {
        options.push_back({id, config.vehicleName});
    }
    return options;
}

} // namespace vehicle_sim::domain
