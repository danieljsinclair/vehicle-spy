#pragma once

#include <string>
#include <memory>

namespace vehicle_sim {

struct VehicleConfig {
    std::string vehicle_id;
    std::string model;
    int year;
    double mass_kg;
    std::string ble_device_address;
    // Add more configuration parameters as needed
};

class ConfigLoader {
public:
    ConfigLoader();
    ~ConfigLoader();

    // Load configuration from file (JSON, YAML, or similar)
    bool loadFromFile(const std::string& filepath) const;

    // Load default configuration
    VehicleConfig getDefaultConfig() const;

    // Get loaded configuration
    VehicleConfig getConfig() const;

    // Validate configuration
    bool validate() const;

private:
    VehicleConfig config_;
};

} // namespace vehicle_sim
