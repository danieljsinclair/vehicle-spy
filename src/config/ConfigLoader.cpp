#include "vehicle-sim/config/ConfigLoader.h"

namespace vehicle_sim {

ConfigLoader::ConfigLoader() = default;

ConfigLoader::~ConfigLoader() = default;

bool ConfigLoader::loadFromFile(const std::string& /*filepath*/) {
    // Placeholder - implement config loading from JSON/YAML
    return true;
}

VehicleConfig ConfigLoader::getDefaultConfig() const {
    VehicleConfig default_config;
    default_config.vehicle_id = "tesla-model-y";
    default_config.model = "Tesla Model Y";
    default_config.year = 2023;
    default_config.mass_kg = 1900.0;
    default_config.ble_device_address = "";
    return default_config;
}

VehicleConfig ConfigLoader::getConfig() const {
    return config_;
}

bool ConfigLoader::validate() const {
    return !config_.vehicle_id.empty();
}

} // namespace vehicle_sim
