#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

namespace vehicle_sim::domain {

VehicleConfig DefaultVehicleConfigs::teslaModel3() {
    // Signal names match the canonical opendbc tesla_model3_party.dbc
    // (resources/dbc/Model3CAN.dbc is that file plus one overlaid message —
    // see the provenance comment at BO_ 994 inside it).
    //   DI_accelPedalPos      -> throttle     (CAN 0x118 / 280, DI_systemStatus)
    //   DI_torqueActual       -> motor torque (CAN 0x108 / 264, DI_torque)
    //   DI_vehicleSpeed       -> speed        (CAN 0x257 / 599, DI_speed)
    //   DI_gear               -> gear         (CAN 0x118 / 280, DI_systemStatus)
    //   SCCM_steeringAngle    -> steering     (CAN 0x129 / 297, SCCM_steeringAngleSensor)
    //   VCLEFT_brakeLightStatus -> brake light (CAN 0x3E2 / 994, ID3E2VCLEFT_lightStatus)
    // Twin-physics rear-motor HV bus (owner-flagged for VirtualIceTwin), added
    // declaratively (DBC def + this mapping, no imperative decode):
    //   DIR_axleSpeed        -> motorRpm      (CAN 0x108 / 264, ID108DIR_torque)
    //   RearHighVoltage126   -> motorHvVoltage(CAN 0x126 / 294, ID126RearHVStatus)
    //   RearMotorCurrent126  -> motorHvCurrent(CAN 0x126 / 294, ID126RearHVStatus)
    // These map onto the existing VehicleSignal HV/RPM fields — see memory
    // [[twin-physics-can-candidates]] (0x108/0x126 present; 0x336/0x185 absent).
    // NOTE on brake: DI_brakePedalState is deliberately NOT mapped — measured
    // on a real Model 3 it is constant 2 (a drive-ready flag, not pedal data).
    // The brake-light enum (0x3E2 bit 0, 2 bits: 0=OFF 1=ON 2=FAULT 3=SNA) is
    // the honest binary pedal proxy; the factory maps only LIGHT_ON(1) to
    // pressed. brakePercent stays unmapped on Tesla (no pressure signal);
    // Audi's ESP_Bremsdruck still populates it. gearRequested is not in the
    // party DBC. accelerationG is not available on Tesla (no longitudinal-accel
    // signal).
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
            {"VCLEFT_brakeLightStatus", "brakeLight"},
            {"DIR_axleSpeed", "motorRpm"},
            {"RearHighVoltage126", "motorHvVoltage"},
            {"RearMotorCurrent126", "motorHvCurrent"}
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
