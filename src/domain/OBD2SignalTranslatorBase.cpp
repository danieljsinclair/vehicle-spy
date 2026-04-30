#include "vehicle-sim/domain/OBD2SignalTranslatorBase.h"

#include <chrono>

namespace vehicle_sim::domain {

OBD2SignalTranslatorBase::OBD2SignalTranslatorBase()
    : lastThrottle_(0.0)
    , lastSpeed_(0.0)
    , lastAcceleration_(0.0)
    , lastBrake_(0.0)
    , lastTimestamp_(0)
    , currentData_(nullptr)
{}

OBD2SignalTranslatorBase::~OBD2SignalTranslatorBase() = default;

bool OBD2SignalTranslatorBase::isValidPacket(
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

std::optional<VehicleSignal> OBD2SignalTranslatorBase::translate(
    const std::vector<std::uint8_t>& rawData
) const noexcept {
    if (!isValidPacket(rawData)) {
        return std::nullopt;
    }

    const std::uint8_t pid = rawData[1];
    std::vector<std::uint8_t> data(rawData.begin() + 2, rawData.end());

    double value = extractPIDValue(pid, data);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        updateSignalField(pid, value);
        lastTimestamp_ = getCurrentTimestamp();

        return VehicleSignal(
            lastThrottle_,
            lastSpeed_,
            lastAcceleration_,
            lastBrake_,
            lastTimestamp_
        );
    }
}

double OBD2SignalTranslatorBase::extractPIDValue(
    std::uint8_t pid,
    const std::vector<std::uint8_t>& data
) const noexcept {
    // Default: no PIDs recognized
    return 0.0;
}

void OBD2SignalTranslatorBase::updateSignalField(
    std::uint8_t pid,
    double value
) const noexcept {
    // Default mapping
    switch (pid) {
        case 0x11: case 0x5A: case 0x5C: lastThrottle_ = value; break;
        case 0x0D: lastSpeed_ = value; break;
        case 0x04: lastAcceleration_ = (value / 100.0) * 2.0 - 1.0; break;
        case 0xA4: lastBrake_ = value; break;
        default: break;
    }
}

std::uint64_t OBD2SignalTranslatorBase::getCurrentTimestamp() const noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace vehicle_sim::domain
