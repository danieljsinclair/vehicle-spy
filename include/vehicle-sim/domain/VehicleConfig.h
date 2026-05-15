#pragma once

#include <string>
#include <unordered_map>
#include "vehicle-sim/domain/DBCSignalDefinition.h"

namespace vehicle_sim::domain {

/**
 * Vehicle configuration data class.
 *
 * Specifies which DBC file to use and how to map DBC signal names
 * to VehicleSignal field names. This is pure data configuration.
 *
 * Adding a new vehicle = providing this config + DBC file.
 * No code changes required (Open/Closed Principle).
 *
 * Example for Tesla Model Y:
 *   - DBC file: "tesla_model3.dbc"
 *   - Signal mappings:
 *     - "DIR_axleSpeed" → "motorRpm"
 *     - "DIR_torqueActual" → "motorTorque"
 *     - "DI_accelPedalPos" → "throttlePercent"
 */
struct VehicleConfig final {
    /**
     * Path to the DBC file for this vehicle.
     *
     * Relative or absolute path. Will be resolved by DBCParser.
     */
    std::string dbcFilePath;

    /**
     * Human-readable vehicle identifier.
     * Used for logging and diagnostics.
     */
    std::string vehicleName;

    /**
     * Map of DBC signal name → VehicleSignal field name.
     *
     * The key is the exact signal name from the DBC file (e.g., "DIR_axleSpeed").
     * The value is the VehicleSignal field to populate (e.g., "motorRpm").
     *
     * This mapping decouples DBC naming from internal field names.
     */
    std::unordered_map<std::string, std::string> signalMappings;

    /**
     * Optional CAN bus identifier (for multi-bus vehicles).
     *
     * e.g., "vehicle", "chassis", "powertrain"
     * Ignored if single-bus vehicle.
     */
    std::string canBus;

    /** True if this vehicle uses raw CAN frames (via DBC), false for standard OBD2 PID responses. */
    bool isCANProtocol = false;

    /**
     * Map of gear codes to human-readable gear labels.
     *
     * The key is the numeric gear code from the vehicle.
     * The value is the display label (e.g., "P", "R", "N", "D", "S").
     */
    std::unordered_map<int, std::string> gearCodeMappings;

    /**
     * Construct a vehicle config.
     *
     * @param dbcFilePath      Path to DBC file
     * @param vehicleName      Human-readable vehicle name
     * @param signalMappings   Map of DBC signal → VehicleSignal field
     * @param canBus           Optional CAN bus name
     * @param isCANProtocol    True for CAN/DBC vehicles, false for OBD2
     * @param gearCodeMappings Map of gear codes to display labels
     */
    VehicleConfig(
        std::string dbcFilePath,
        std::string vehicleName,
        std::unordered_map<std::string, std::string> signalMappings,
        std::string canBus = "",
        bool isCANProtocol = false,
        std::unordered_map<int, std::string> gearCodeMappings = {}
    ) noexcept;

    // Default copy/move
    VehicleConfig(const VehicleConfig&) noexcept = default;
    VehicleConfig(VehicleConfig&&) noexcept = default;
    VehicleConfig& operator=(const VehicleConfig&) noexcept = default;
    VehicleConfig& operator=(VehicleConfig&&) noexcept = default;
    ~VehicleConfig() noexcept = default;

    // Equality comparison (all fields)
    [[nodiscard]] bool operator==(
        const VehicleConfig& other
    ) const noexcept;

    // Inequality (derived from operator==)
    [[nodiscard]] bool operator!=(
        const VehicleConfig& other
    ) const noexcept;

    /**
     * Check if a DBC signal is mapped to a VehicleSignal field.
     */
    [[nodiscard]] bool hasMapping(
        const std::string& signalName
    ) const noexcept;

    /**
     * Get the VehicleSignal field name for a DBC signal.
     *
     * @param signalName DBC signal name
     * @return VehicleSignal field name, or empty string if not mapped
     */
    [[nodiscard]] std::string getFieldForSignal(
        const std::string& signalName
    ) const noexcept;
};

/**
 * Vehicle configuration registry.
 *
 * Holds all vehicle configs and provides lookup by vehicle type identifier.
 * This allows runtime vehicle selection without hardcoding.
 */
class VehicleConfigRegistry final {
public:
    /**
     * Register a vehicle configuration.
     *
     * @param vehicleId     Unique identifier (e.g., "tesla_model_y", "audi_etron")
     * @param config        Vehicle configuration
     */
    void registerVehicle(
        const std::string& vehicleId,
        VehicleConfig config
    ) noexcept;

    /**
     * Get a vehicle configuration by ID.
     *
     * @param vehicleId Vehicle identifier
     * @return Pointer to config, or nullptr if not found
     */
    [[nodiscard]] const VehicleConfig* getConfig(
        const std::string& vehicleId
    ) const noexcept;

    /**
     * Check if a vehicle ID is registered.
     */
    [[nodiscard]] bool hasConfig(
        const std::string& vehicleId
    ) const noexcept;

    /**
     * Get all registered vehicle IDs.
     */
    [[nodiscard]] std::vector<std::string> getRegisteredVehicles() const noexcept;

private:
    std::unordered_map<std::string, VehicleConfig> configs_;
};

} // namespace vehicle_sim::domain
