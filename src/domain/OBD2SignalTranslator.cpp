#include "vehicle-sim/domain/OBD2SignalTranslator.h"

#include <chrono>
#include <cmath>

namespace vehicle_sim::domain {

OBD2SignalTranslator::OBD2SignalTranslator()
    : lastThrottle_(0.0)
    , lastSpeed_(0.0)
    , lastAcceleration_(0.0)
    , lastBrake_(0.0)
    , lastTimestamp_(0)
{}

bool OBD2SignalTranslator::isValidPacket(
    const std::vector<std::uint8_t>& rawData
) const noexcept {
    // Minimum: mode byte + PID byte + at least 1 data byte
    if (rawData.size() < 3) {
        return false;
    }

    // Must be a response mode (0x40-0x4F for Mode 01-0F responses)
    if (rawData[0] < 0x40 || rawData[0] > 0x4F) {
        return false;
    }

    return true;
}

std::optional<VehicleSignal> OBD2SignalTranslator::translate(
    const std::vector<std::uint8_t>& rawData
) const noexcept {
    if (!isValidPacket(rawData)) {
        return std::nullopt;
    }

    const std::uint8_t pid = rawData[1];

    // Data bytes start at index 2
    std::vector<std::uint8_t> data(rawData.begin() + 2, rawData.end());

    // Extract the PID-specific value
    double value = extractPID(pid, data);

    // Update accumulated state
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        switch (pid) {
            case 0x11:  // Throttle position
                lastThrottle_ = value;
                break;
            case 0x0D:  // Vehicle speed
                lastSpeed_ = value;
                break;
            case 0x04:  // Engine load → acceleration proxy
                lastAcceleration_ = (value / 100.0) * 2.0 - 1.0;  // Scale to G range
                break;
            case 0x5A:  // Accelerator position D (alternate throttle)
                lastThrottle_ = value;
                break;
            default:
                // Unknown PID — don't update any field
                break;
        }

        lastTimestamp_ = getCurrentTimestamp();

        // Build signal from accumulated state
        return VehicleSignal(
            lastThrottle_,
            lastSpeed_,
            lastAcceleration_,
            lastBrake_,
            lastTimestamp_
        );
    }
}

double OBD2SignalTranslator::extractPID(
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
            return (static_cast<double>(data[0]) / 255.0) * 100.0;

        case 0x04:  // Engine load: (A / 255) * 100
            return (static_cast<double>(data[0]) / 255.0) * 100.0;

        case 0x0C:  // Engine RPM: ((A * 256) + B) / 4
            if (data.size() >= 2) {
                return ((static_cast<double>(data[0]) * 256.0) +
                         static_cast<double>(data[1])) / 4.0;
            }
            return static_cast<double>(data[0]);

        case 0x05:  // Coolant temp: A - 40
            return static_cast<double>(data[0]) - 40.0;

        case 0x2F:  // Fuel level: (A / 255) * 100
            return (static_cast<double>(data[0]) / 255.0) * 100.0;

        case 0x5A:  // Accelerator position D: (A / 255) * 100
            return (static_cast<double>(data[0]) / 255.0) * 100.0;

        default:
            return static_cast<double>(data[0]);
    }
}

std::uint64_t OBD2SignalTranslator::getCurrentTimestamp() const noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace vehicle_sim::domain
