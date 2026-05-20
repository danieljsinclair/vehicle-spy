#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

namespace vehicle_sim::domain {

VehicleConfig DefaultVehicleConfigs::teslaModel3() {
    return VehicleConfig(
        "resources/dbc/Model3CAN.dbc",
        "Model3CAN.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_motorRPM", "motorRpm"},
            {"DI_torqueMotor", "motorTorqueNm"},
            {"DI_pedalPos", "throttlePercent"},
            {"DI_gear", "gearSelector"},
            {"DI_gearRequest", "gearRequested"},
            {"DI_vehicleSpeed", "speedKmh"},
            {"DI_brakePedal", "brakePercent"}
        },
        "",  // canBus
        true  // isCANProtocol
    );
}

VehicleConfig DefaultVehicleConfigs::audiMLBEvo() {
    return VehicleConfig(
        "resources/dbc/vw_mlb.dbc",
        "vw_mlb.dbc",
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

VehicleConfig DefaultVehicleConfigs::generic() {
    return VehicleConfig(
        "",  // No DBC for generic OBD2
        "",  // No bundle file
        "Generic OBD2",
        std::unordered_map<std::string, std::string>{},
        "",   // canBus
        false  // isCANProtocol (false = OBD2)
    );
}

void DefaultVehicleConfigs::registerAll(VehicleConfigRegistry& registry) {
    registry.registerVehicle("generic", generic());
    registry.registerVehicle("tesla", teslaModel3());
    registry.registerVehicle("audi_mlb_evo", audiMLBEvo());
}

} // namespace vehicle_sim::domain
