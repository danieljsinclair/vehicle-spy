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
        const std::vector<std::uint8_t>& rawData,
        std::optional<std::uint64_t> timestampUtcMs = std::nullopt
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
     * NOTE: invoked by translate() while state_mutex_ is held; overriding
     * implementations must NOT acquire state_mutex_ (it is non-recursive and
     * would deadlock) and must mutate the last-known state via the protected
     * setLast*() helpers only.
     *
     * The `lockProof` parameter is a capability token: it is a reference to the
     * std::scoped_lock that translate() holds on state_mutex_, so the compiler
     * guarantees every caller of updateSignalField()/setLast*() already holds the
     * lock (you cannot construct the proof without taking it). It is not used at
     * runtime. This makes the previously comment-only lock contract type-enforced.
     *
     * @param pid The PID that was extracted
     * @param value The numeric value extracted
     * @param lockProof Reference to the scoped_lock translate() holds on
     *                 state_mutex_ — proof the caller holds the lock.
     */
    virtual void updateSignalField(
        std::uint8_t pid,
        double value,
        std::scoped_lock<std::mutex>& lockProof
    ) const noexcept;

    [[nodiscard]] std::uint64_t getCurrentTimestamp() const noexcept;

private:
    // State accumulated across multiple PID responses. Private so all access
    // is channelled through translate()'s single locked read/modify/snapshot.
    mutable std::mutex state_mutex_;

    // Last-known values (updated when matching PID is seen)
    mutable double lastThrottle_ = 0.0;
    mutable double lastSpeed_ = 0.0;
    mutable double lastAcceleration_ = 0.0;
    mutable double lastBrake_ = 0.0;
    mutable std::uint64_t lastTimestamp_ = 0;

    // Pre-locked write helpers for the accumulated state. Private so the only
    // callers are updateSignalField()/translate(), both of which run while
    // state_mutex_ is held — keeping every mutation behind the lock. Each takes
    // a scoped_lock& capability token (the lock translate() holds) so the lock
    // contract is type-enforced: the helpers cannot be called without already
    // holding state_mutex_. The token is unused at runtime.
    void setLastThrottle(double v, std::scoped_lock<std::mutex>&) const noexcept { lastThrottle_ = v; }
    void setLastSpeed(double v, std::scoped_lock<std::mutex>&) const noexcept { lastSpeed_ = v; }
    void setLastAcceleration(double v, std::scoped_lock<std::mutex>&) const noexcept { lastAcceleration_ = v; }
    void setLastBrake(double v, std::scoped_lock<std::mutex>&) const noexcept { lastBrake_ = v; }
};

} // namespace vehicle_sim::domain
