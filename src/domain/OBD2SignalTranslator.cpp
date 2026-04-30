#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/OBD2SignalTranslatorBase.h"

#include <cmath>

namespace vehicle_sim::domain {

OBD2SignalTranslator::OBD2SignalTranslator() = default;

// translate() and isValidPacket() are inherited from base class — no override needed
// Base implementation handles: validation, state accumulation, VehicleSignal construction

double OBD2SignalTranslator::extractPIDValue(
    std::uint8_t pid,
    const std::vector<std::uint8_t>& data
) const noexcept {
    if (data.empty()) {
        return 0.0;
    }

    switch (pid) {
        case 0x0D:  // Vehicle speed: A = km/h
            return static_cast<double>(data[0]);

        case 0x11:  // Throttle position: (A / 255) * 100
            return (static_cast<double>(data[0]) / OBD2_MAX_BYTE) * OBD2_PERCENT_SCALE;

        case 0x04:  // Engine load: (A / 255) * 100
            return (static_cast<double>(data[0]) / OBD2_MAX_BYTE) * OBD2_PERCENT_SCALE;

        case 0x0C:  // Engine RPM: ((A * 256) + B) / 4
            if (data.size() >= 2) {
                return ((static_cast<double>(data[0]) * 256.0) +
                         static_cast<double>(data[1])) / OBD2_RPM_DIVISOR;
            }
            return static_cast<double>(data[0]);

        case 0x05:  // Coolant temp: A - 40
            return static_cast<double>(data[0]) - OBD2_TEMP_OFFSET;

        case 0x2F:  // Fuel level: (A / 255) * 100
            return (static_cast<double>(data[0]) / OBD2_MAX_BYTE) * OBD2_PERCENT_SCALE;

        case 0x5A:  // Accelerator position D: (A / 255) * 100
            return (static_cast<double>(data[0]) / OBD2_MAX_BYTE) * OBD2_PERCENT_SCALE;

        default:
            return static_cast<double>(data[0]);
    }
}

// getCurrentTimestamp() is inherited from base class

} // namespace vehicle_sim::domain
