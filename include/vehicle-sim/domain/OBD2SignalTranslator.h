#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <mutex>
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace vehicle_sim::domain {

/**
 * Generic OBD2 signal translator (SAE J1979)
 *
 * Translates standard OBD2 Mode 01 responses into VehicleSignal.
 * Works with any ELM327-compatible BLE adapter connected to standard
 * OBD2 ports (Toyota, Ford, VW, etc.).
 *
 * Maintains state across PID responses to build a complete VehicleSignal
 * from individual PID queries (speed, throttle, etc. arrive one at a time).
 */
class OBD2SignalTranslator final : public ISignalTranslator {
public:
    OBD2SignalTranslator();
    ~OBD2SignalTranslator() override = default;

    OBD2SignalTranslator(const OBD2SignalTranslator&) = delete;
    OBD2SignalTranslator& operator=(const OBD2SignalTranslator&) = delete;

    [[nodiscard]] std::optional<VehicleSignal> translate(
        const std::vector<std::uint8_t>& rawData
    ) const noexcept override;

    [[nodiscard]] bool isValidPacket(
        const std::vector<std::uint8_t>& rawData
    ) const noexcept override;

private:
    // OBD2 response mode for Mode 01
    static constexpr std::uint8_t MODE_01_RESPONSE = 0x41;

    // State accumulated across multiple PID responses
    mutable std::mutex state_mutex_;
    mutable double lastThrottle_ = 0.0;
    mutable double lastSpeed_ = 0.0;
    mutable double lastAcceleration_ = 0.0;
    mutable double lastBrake_ = 0.0;
    mutable std::uint64_t lastTimestamp_ = 0;

    [[nodiscard]] double extractPID(
        std::uint8_t pid,
        const std::vector<std::uint8_t>& data
    ) const noexcept;

    [[nodiscard]] std::uint64_t getCurrentTimestamp() const noexcept;
};

} // namespace vehicle_sim::domain
