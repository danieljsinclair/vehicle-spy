#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <mutex>
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace vehicle_sim::domain {

/**
 * Base class for OBD2-based signal translators.
 *
 * Provides common OBD2 infrastructure:
 * - State management (last-known values for all 4 VehicleSignal fields)
 * - Timestamp handling
 * - Default PID extraction via virtual hook methods
 * - Response validation (mode 0x41, minimum length)
 *
 * Subclasses override specific PID extraction to customize which PIDs
 * they recognize and how they decode them. This keeps code DRY — common
 * OBD2 logic (state accumulation, VehicleSignal construction) is shared.
 *
 * Example:
 *   class GenericOBD2Translator : public OBD2SignalTranslatorBase { ... }
 *   class AudiETronTranslator : public OBD2SignalTranslatorBase { ... }
 */
class OBD2SignalTranslatorBase : public ISignalTranslator {
public:
    OBD2SignalTranslatorBase();
    virtual ~OBD2SignalTranslatorBase();

    OBD2SignalTranslatorBase(const OBD2SignalTranslatorBase&) = delete;
    OBD2SignalTranslatorBase& operator=(const OBD2SignalTranslatorBase&) = delete;

    // ISignalTranslator interface
    [[nodiscard]] bool isValidPacket(
        const std::vector<std::uint8_t>& rawData
    ) const noexcept override;

    [[nodiscard]] std::optional<VehicleSignal> translate(
        const std::vector<std::uint8_t>& rawData
    ) const noexcept override;

protected:
    // OBD2 response validation
    static constexpr std::uint8_t RESPONSE_MODE = 0x41;
    static constexpr std::uint8_t RESPONSE_MODE_MIN = 0x40;  // 0x40-0x4F = Mode 01-0F responses
    static constexpr std::uint8_t RESPONSE_MODE_MAX = 0x4F;
    static constexpr std::size_t DATA_OFFSET = 2;             // skip mode + pid bytes

    // OBD2 Mode 01 PIDs (SAE J1979)
    static constexpr std::uint8_t PID_ENGINE_LOAD = 0x04;
    static constexpr std::uint8_t PID_COOLANT_TEMP = 0x05;
    static constexpr std::uint8_t PID_ENGINE_RPM = 0x0C;
    static constexpr std::uint8_t PID_VEHICLE_SPEED = 0x0D;
    static constexpr std::uint8_t PID_INTAKE_AIR_TEMP = 0x0F;
    static constexpr std::uint8_t PID_THROTTLE_POSITION = 0x11;
    static constexpr std::uint8_t PID_FUEL_LEVEL = 0x2F;
    static constexpr std::uint8_t PID_ACCELERATOR_POS_D = 0x5A;
    static constexpr std::uint8_t PID_ACCELERATOR_POS_P = 0x5C;
    static constexpr std::uint8_t PID_BRAKE_PRESSURE = 0xA4;

    // State accumulated across multiple PID responses
    mutable std::mutex state_mutex_;

    // Last-known values (updated when matching PID is seen)
    mutable double lastThrottle_ = 0.0;
    mutable double lastSpeed_ = 0.0;
    mutable double lastAcceleration_ = 0.0;
    mutable double lastBrake_ = 0.0;
    mutable std::uint64_t lastTimestamp_ = 0;

    // Current raw data being processed (subclass hook access)
    mutable const std::vector<uint8_t>* currentData_ = nullptr;

    /**
     * Extract a value from OBD2 response data for the given PID.
     * Override this in subclasses to provide custom decoding per PID.
     *
     * @param pid The PID from the response
     * @param data Data bytes (starting after mode+pid)
     * @return Numeric value for this PID, or 0.0 if unrecognized
     */
    [[nodiscard]] virtual double extractPIDValue(
        std::uint8_t pid,
        const std::vector<std::uint8_t>& data
    ) const noexcept;

    /**
     * Update state after extracting a PID value.
     * Override to customize which VehicleSignal field each PID updates.
     *
     * Default: stores value directly by field name matched to PID.
     * Subclasses can override to implement custom mapping.
     *
     * @param pid The PID that was extracted
     * @param value The numeric value extracted
     */
    virtual void updateSignalField(std::uint8_t pid, double value) const noexcept;

    [[nodiscard]] std::uint64_t getCurrentTimestamp() const noexcept;
};

} // namespace vehicle_sim::domain
