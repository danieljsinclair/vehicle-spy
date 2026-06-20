#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

namespace vehicle_sim::domain {

VehicleConfig DefaultVehicleConfigs::teslaModel3() {
    // Signal names match the canonical opendbc tesla_model3_party.dbc
    // (resources/dbc/Model3CAN.dbc is that file, verbatim — no hand-edits).
    //   DI_accelPedalPos   -> throttle     (CAN 0x118 / 280, DI_systemStatus)
    //   DI_torqueActual    -> motor torque (CAN 0x108 / 264, DI_torque)
    //   DI_vehicleSpeed    -> speed        (CAN 0x257 / 599, DI_speed)
    //   DI_gear            -> gear         (CAN 0x118 / 280, DI_systemStatus)
    //   SCCM_steeringAngle -> steering     (CAN 0x129 / 297, SCCM_steeringAngleSensor)
    //   DI_brakePedalState -> brake        (CAN 0x118 / 280, DI_systemStatus)
    // NOTE on brake: DI_brakePedalState is a 2-bit pedal-applied switch
    // (VAL_ 0=OFF/1=ON/2=INVALID), NOT a brake-pressure percentage. We map it
    // to brakePercent as a truthful pedal-applied indicator (0=off, 1=on) so
    // the column is populated honestly rather than left blank or fabricated.
    // A direct 0/1 is intentionally NOT scaled to 0/100: overclaiming brake
    // magnitude from a 2-bit switch would repeat the impossible-value class of
    // bug seen earlier with throttle. gearRequested is not in the party DBC.
    // accelerationG is not available on Tesla (no longitudinal-accel signal).
    return VehicleConfig(
        "resources/dbc/Model3CAN.dbc",
        "Model3CAN.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_accelPedalPos", "throttlePercent"},
            {"DI_torqueActual", "motorTorqueNm"},
            {"DI_vehicleSpeed", "speedKmh"},
            {"DI_gear", "gearSelector"},
            {"SCCM_steeringAngle", "steeringAngleDeg"},
            {"DI_brakePedalState", "brakePercent"}
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
