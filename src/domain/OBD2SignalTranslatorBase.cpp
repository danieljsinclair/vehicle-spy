#include "vehicle-sim/domain/OBD2SignalTranslatorBase.h"

#include <chrono>

namespace vehicle_sim::domain {

OBD2SignalTranslatorBase::OBD2SignalTranslatorBase() = default;

OBD2SignalTranslatorBase::~OBD2SignalTranslatorBase() = default;

bool OBD2SignalTranslatorBase::isValidPacket(
    const std::vector<std::uint8_t>& rawData
) const noexcept {
    // Minimum: mode byte + PID byte + at least 1 data byte
    if (rawData.size() < DATA_OFFSET + 1) {
        return false;
    }
    // Must be a response mode (0x40-0x4F for Mode 01-0F responses)
    if (rawData[0] < RESPONSE_MODE_MIN || rawData[0] > RESPONSE_MODE_MAX) {
        return false;
    }
    return true;
}

std::optional<VehicleSignal> OBD2SignalTranslatorBase::translate(
    const std::vector<std::uint8_t>& rawData,
    std::optional<std::uint64_t> timestampUtcMs
) const noexcept {
    if (!isValidPacket(rawData)) {
        return std::nullopt;
    }

    const std::uint8_t pid = rawData[1];
    std::vector<std::uint8_t> data(rawData.begin() + DATA_OFFSET, rawData.end());

    double value = extractPIDValue(pid, data);

    // Stamp the emitted signal with the original capture time when supplied
    // (replay path); otherwise fall back to wall-clock now() (live/BLE path).
    const std::uint64_t effectiveTs = timestampUtcMs.value_or(getCurrentTimestamp());

    {
        std::scoped_lock lock(state_mutex_);
        updateSignalField(pid, value);
        lastTimestamp_ = effectiveTs;

        return VehicleSignal(
            lastTimestamp_,
            lastThrottle_,
            lastSpeed_,
            lastAcceleration_,
            lastBrake_,
            std::nullopt,  // steeringAngleDeg
            std::nullopt,  // motorRpm
            std::nullopt,  // motorHvVoltage
            std::nullopt,  // motorHvCurrent
            std::nullopt,  // motorTorqueNm
            std::nullopt   // gearSelector
        );
    }
}

double OBD2SignalTranslatorBase::extractPIDValue(
    std::uint8_t /*pid*/,
    const std::vector<std::uint8_t>& /*data*/
) const noexcept {
    // Default: no PIDs recognized
    return 0.0;
}

void OBD2SignalTranslatorBase::updateSignalField(
    std::uint8_t pid,
    double value
) const noexcept {
    // Default mapping. translate() holds state_mutex_ across this call and the
    // subsequent snapshot, so the last-known state is updated atomically; route
    // the writes through the pre-locked setLast*() helpers (see header — they
    // must NOT lock, since state_mutex_ is already held here).
    switch (pid) {
        case PID_THROTTLE_POSITION: case PID_ACCELERATOR_POS_D: case PID_ACCELERATOR_POS_P: setLastThrottle(value); break;
        case PID_VEHICLE_SPEED: setLastSpeed(value); break;
        case PID_ENGINE_LOAD: setLastAcceleration((value / 100.0) * 2.0 - 1.0); break;
        case PID_BRAKE_PRESSURE: setLastBrake(value); break;
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
