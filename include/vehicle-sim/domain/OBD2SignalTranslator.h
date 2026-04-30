#pragma once

#include <vector>
#include <cstdint>
#include "vehicle-sim/domain/OBD2SignalTranslatorBase.h"

namespace vehicle_sim::domain {

/**
 * Generic OBD2 signal translator (SAE J1979)
 *
 * Translates standard OBD2 Mode 01 responses into VehicleSignal.
 * Works with any ELM327-compatible BLE adapter connected to standard
 * OBD2 ports (Toyota, Ford, VW, etc.).
 *
 * Inherits all infrastructure from OBD2SignalTranslatorBase.
 * Only decode logic (extractPIDValue) is implemented here.
 */
class OBD2SignalTranslator : public OBD2SignalTranslatorBase {
public:
    OBD2SignalTranslator();
    ~OBD2SignalTranslator() override = default;

    OBD2SignalTranslator(const OBD2SignalTranslator&) = delete;
    OBD2SignalTranslator& operator=(const OBD2SignalTranslator&) = delete;

protected:
    // SAE J1979 scaling constants
    static constexpr double OBD2_MAX_BYTE = 255.0;       // 8-bit A value maximum
    static constexpr double OBD2_PERCENT_SCALE = 100.0;  // (A / 255) * 100
    static constexpr double OBD2_RPM_DIVISOR = 4.0;      // ((A * 256) + B) / 4
    static constexpr double OBD2_TEMP_OFFSET = 40.0;     // A - 40 (coolant/intake temp)

    [[nodiscard]] double extractPIDValue(
        std::uint8_t pid,
        const std::vector<std::uint8_t>& data
    ) const noexcept override;
};

} // namespace vehicle_sim::domain
