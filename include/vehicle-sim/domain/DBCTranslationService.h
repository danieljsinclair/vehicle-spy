#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCParser.h"

namespace vehicle_sim::domain {

enum class VehicleProtocol {
    CAN,
    OBD2,
    Simulation
};

class DBCTranslationService {
public:
    DBCTranslationService();
    ~DBCTranslationService();

    bool loadVehicle(const std::string& vehicleId, VehicleProtocol protocol);
    bool loadVehicleWithContent(const std::string& vehicleId, VehicleProtocol protocol, const std::string& dbcContent);
    bool loadVehicleFromPath(const std::string& vehicleId, VehicleProtocol protocol, const std::string& dbcAbsolutePath);
    [[nodiscard]] std::optional<VehicleSignal> processFrame(const std::vector<std::uint8_t>& rawData) const noexcept;
    [[nodiscard]] VehicleProtocol getProtocol() const noexcept;
    [[nodiscard]] std::string getVehicleId() const noexcept;
    [[nodiscard]] bool isLoaded() const noexcept;

    VehicleConfigRegistry& registry();
    const VehicleConfigRegistry& registry() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace vehicle_sim::domain
