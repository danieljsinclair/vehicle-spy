#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

namespace vehicle_sim::domain {

VehicleConfig DefaultVehicleConfigs::teslaModel3() {
    return VehicleConfig(
        "resources/dbc/Model3CAN.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        },
        "",  // canBus
        true, // isCANProtocol
        {{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );
}

VehicleConfig DefaultVehicleConfigs::audiMLBEvo() {
    return VehicleConfig(
        "resources/dbc/vw_mlb.dbc",
        "Audi MLB Evo",
        std::unordered_map<std::string, std::string>{
            {"ESP_v_Signal", "speedKmh"},
            {"ESP_Laengsbeschl", "accelerationG"},
            {"ESP_Bremsdruck", "brakePercent"}
        },
        "",  // canBus
        true // isCANProtocol
    );
}

void DefaultVehicleConfigs::registerAll(VehicleConfigRegistry& registry) {
    registry.registerVehicle("tesla_model3", teslaModel3());
    registry.registerVehicle("audi_mlb_evo", audiMLBEvo());
}

} // namespace vehicle_sim::domain
