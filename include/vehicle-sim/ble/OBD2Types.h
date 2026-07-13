#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace vehicle_sim {

/**
 * @brief Common OBD2 BLE UUIDs used by ELM327-compatible adapters.
 *
 * These UUIDs are standardized across most BLE OBD2 adapters (Vgate, OBDLink, etc.)
 */
struct OBD2UUIDs {
    // Nordic UART Service (NUS) - common for ELM327 BLE
    static constexpr const char* NUS_SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    static constexpr const char* NUS_TX_CHARACTERISTIC = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // Notify
    static constexpr const char* NUS_RX_CHARACTERISTIC = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // Write

    // Legacy OBD2 UART (older ELM327 clones)
    static constexpr const char* OBD2_SERVICE = "FFE0";
    static constexpr const char* OBD2_CHARACTERISTIC = "FFE1";

    // Standard BLE services
    static constexpr const char* BATTERY_SERVICE = "180F";
    static constexpr const char* DEVICE_INFO_SERVICE = "180A";
};

/**
 * @brief OBD2 PID constants for standard vehicle telemetry.
 *
 * These PIDs follow the OBD-II standard (SAE J1979).
 */
struct OBD2PIDs {
    // Mode 01 (Live Data) PIDs
    static constexpr uint8_t THROTTLE_POSITION = 0x11;      // 0-100%
    static constexpr uint8_t VEHICLE_SPEED = 0x0D;          // km/h
    static constexpr uint8_t ENGINE_RPM = 0x0C;            // RPM
    static constexpr uint8_t COOLANT_TEMP = 0x05;           // Celsius
    static constexpr uint8_t INTAKE_AIR_TEMP = 0x0F;        // Celsius
    static constexpr uint8_t ENGINE_LOAD = 0x04;            // 0-100%
    static constexpr uint8_t FUEL_LEVEL = 0x2F;             // 0-100%
    static constexpr uint8_t BATTERY_VOLTAGE = 0x42;        // V (control module voltage)
    static constexpr uint8_t ENGINE_RUNTIME = 0x1F;         // seconds since engine start
    static constexpr uint8_t BRAKE_PRESSURE = 0xA4;         // Manufacturer specific
    static constexpr uint8_t ACCELERATOR_POSITION_D = 0x5A; // Driver pedal %
    static constexpr uint8_t ACCELERATOR_POSITION_P = 0x5C; // Passenger pedal %
};

/**
 * @brief Parsed OBD2 response containing a telemetry value.
 *
 * Kept as the return type of Elm327Session::queryPID. New OBD2 parsing
 * should prefer ELM327Transport::parseOBD2Response + OBD2Protocol.
 */
struct OBD2Response {
    uint8_t mode = 0;
    uint8_t pid = 0;
    std::vector<uint8_t> data;
    std::optional<double> value;
    bool valid = false;
};

} // namespace vehicle_sim
