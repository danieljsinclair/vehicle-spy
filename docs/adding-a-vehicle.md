# Adding a New Vehicle

## Steps

### Step 1: Add DBC File
Copy the DBC file to `resources/dbc/`:
```bash
cp /path/to/vehicle.dbc resources/dbc/VehicleName.dbc
```

If the DBC is from commaai/opendbc, use `make update-dbc`:
```bash
make update-dbc
```

### Step 2: Add Signal Mapping
Edit `src/domain/DefaultVehicleConfigs.cpp` and add a new function:
```cpp
VehicleConfig DefaultVehicleConfigs::vehicleName() {
    return VehicleConfig(
        "resources/dbc/VehicleName.dbc",  // DBC path
        "VehicleName.dbc",                  // Bundle filename for iOS
        "Vehicle Display Name",
        std::unordered_map<std::string, std::string>{
            {"DBC_signal_name", "targetField"},
            // Add more mappings as needed
        },
        "",   // canBus (empty = auto-detect)
        false  // isCANProtocol
    );
}
```

Register it in `registerAll()`:
```cpp
void DefaultVehicleConfigs::registerAll(VehicleConfigRegistry& registry) {
    registry.registerVehicle("tesla_model3", teslaModel3());
    registry.registerVehicle("audi_mlb_evo", audiMLBEvo());
    registry.registerVehicle("vehicle_name", vehicleName());  // Add this
}
```

### Step 3: That's It
The DBC parser, signal factory, and display layer handle everything else automatically. No manual translator classes needed.

## Worked Example: Tesla Model 3

```cpp
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
        "",
        true  // isCANProtocol
    );
}
```

## Troubleshooting

**DBC not found**: Check the file path matches exactly between `VehicleConfig` constructor and `resources/dbc/`.

**Signal not appearing**: Verify the signal name in the DBC matches the mapping key exactly (case-sensitive).

**Gear showing wrong value**: Check the DBC VAL_ table format. It should use identifiers like `DI_GEAR_P`, `DI_GEAR_D`, etc., for automatic translation to `Gear::` constants.