#pragma once

#include "vehicle-sim/domain/OBD2SignalTranslator.h"

namespace vehicle_sim::domain {

/**
 * Audi eTron 2021 OBD2 signal translator.
 *
 * Extends generic OBD2 decoding with Audi-specific manufacturer PIDs.
 * Reuses standard OBD2 decoding via parent OBD2SignalTranslator,
 * and only overrides extraction for the Audi-specific PIDs.
 *
 * PID mapping (Audi eTron 2021):
 *   0xFE → Battery SOC (%): mapped to throttle field as SOC proxy
 *   0xFD → HV battery voltage (V): 2 bytes little-endian
 *   0xFC → Charging current (A): 2 bytes, 0.1A per count
 */
class AudiSignalTranslator final : public OBD2SignalTranslator {
public:
    AudiSignalTranslator();
    ~AudiSignalTranslator() override = default;
    AudiSignalTranslator(const AudiSignalTranslator&) = delete;
    AudiSignalTranslator& operator=(const AudiSignalTranslator&) = delete;

protected:
    // Audi-specific PID constants
    static constexpr std::uint8_t PID_BATTERY_SOC = 0xFE;
    static constexpr std::uint8_t PID_HV_VOLTAGE = 0xFD;
    static constexpr std::uint8_t PID_CHARGE_CURRENT = 0xFC;
    static constexpr double CHARGE_CURRENT_SCALE = 0.1;  // 0.1A per count

    [[nodiscard]] double extractPIDValue(
        std::uint8_t pid,
        const std::vector<std::uint8_t>& data
    ) const noexcept override;

    // Map Audi-specific PIDs to VehicleSignal fields
    void updateSignalField(std::uint8_t pid, double value) const noexcept override;
};

} // namespace vehicle_sim::domain
