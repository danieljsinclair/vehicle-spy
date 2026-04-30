#include "vehicle-sim/domain/AudiSignalTranslator.h"

namespace vehicle_sim::domain {

AudiSignalTranslator::AudiSignalTranslator() = default;

double AudiSignalTranslator::extractPIDValue(
    std::uint8_t pid,
    const std::vector<std::uint8_t>& data
) const noexcept {
    switch (pid) {
        // Battery State of Charge — 1 byte, 0–100%
        case PID_BATTERY_SOC: {
            if (data.empty()) return 0.0;
            return static_cast<double>(data[0]);
        }

        // High Voltage (HV) battery voltage — 2 bytes little-endian, volts
        case PID_HV_VOLTAGE: {
            if (data.size() < 2) return 0.0;
            return static_cast<double>(data[0] | (data[1] << 8));
        }

        // Charging current — 2 bytes little-endian, 0.1A per count
        case PID_CHARGE_CURRENT: {
            if (data.size() < 2) return 0.0;
            return (data[0] | (data[1] << 8)) * CHARGE_CURRENT_SCALE;
        }

        // Defer all other PIDs to parent (standard OBD2 decoding)
        default: {
            return OBD2SignalTranslator::extractPIDValue(pid, data);
        }
    }
}

void AudiSignalTranslator::updateSignalField(
    std::uint8_t pid,
    double value
) const noexcept {
    // Audi-specific custom mappings
    switch (pid) {
        case PID_HV_VOLTAGE: {
            lastSpeed_ = value;
            break;
        }
        case PID_BATTERY_SOC:  lastThrottle_ = value; break;        // SOC → throttle proxy
        case PID_CHARGE_CURRENT:  lastAcceleration_ = value; break; // Charge current → acceleration (positive = charging)
        default: {
            // Fall back to parent's mapping for standard OBD2 PIDs
            OBD2SignalTranslator::updateSignalField(pid, value);
            break;
        }
    }
}

} // namespace vehicle_sim::domain
