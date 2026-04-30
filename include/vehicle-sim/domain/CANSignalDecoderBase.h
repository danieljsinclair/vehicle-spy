#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <functional>
#include <unordered_map>
#include <memory>
#include "vehicle-sim/domain/ITimeProvider.h"
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Shared base class for CAN signal translators.
 *
 * Implements DRY by extracting common functionality:
 * - Shared state accumulation fields
 * - extractCANId() method
 * - buildSignal() method
 * - Decoder registry for Open/Closed compliance
 * - Shared decodeCAN280() and decodeCAN297() methods
 *
 * Design principles:
 * - SRP: Separates CAN frame parsing from signal decoding
 * - DRY: Eliminates code duplication between translators
 * - OCP: New CAN IDs added via decoder map without modifying base
 * - DIP: Uses ITimeProvider for testable time source
 *
 * Subclasses construct decoder maps in their constructors using lambdas
 * that call protected setter methods (setSpeedKmh, setThrottlePercent, etc.).
 * Each setter enforces the valid range for its field.
 *
 * Usage:
 *   class TeslaTranslator : public CANSignalDecoderBase {
 *   public:
 *       TeslaTranslator()
 *           : CANSignalDecoderBase(
 *               std::make_unique<SystemTimeProvider>(),
 *               buildDecoders()) {}
 *   private:
 *       static DecoderMap buildDecoders() {
 *           return { {280, [](const auto& d) { ... }} };
 *       }
 *   };
 */
class CANSignalDecoderBase {
public:
    using DecoderMap = std::unordered_map<uint16_t,
        std::function<void(const std::vector<uint8_t>&)>>;

    /// @pre timeProvider must not be nullptr
    explicit CANSignalDecoderBase(
        std::unique_ptr<ITimeProvider> timeProvider,
        DecoderMap decoders
    );

    virtual ~CANSignalDecoderBase() = default;

    CANSignalDecoderBase(const CANSignalDecoderBase&) = delete;
    CANSignalDecoderBase& operator=(const CANSignalDecoderBase&) = delete;

    /**
     * Process a single CAN frame.
     *
     * Frame format: [canId_lo, canId_hi, data_byte_0, ..., data_byte_7]
     *
     * @param frame Raw CAN frame (minimum 10 bytes: 2 ID + 8 data)
     * @return VehicleSignal if supported CAN ID decoded, std::nullopt otherwise
     */
    [[nodiscard]] std::optional<VehicleSignal> translateFrame(
        const std::vector<uint8_t>& frame
    ) const noexcept;

    /**
     * Validate whether a frame is from a supported CAN ID.
     *
     * @param frame Raw CAN frame
     * @return true if CAN ID is in decoder registry
     */
    [[nodiscard]] bool isValidFrame(
        const std::vector<uint8_t>& frame
    ) const noexcept;

protected:
    static constexpr std::size_t CAN_DATA_OFFSET = 2;
    static constexpr std::size_t CAN_FRAME_SIZE = 10;

    using DecoderFunction = std::function<void(const std::vector<uint8_t>&)>;

    /**
     * Extract CAN ID from raw frame.
     *
     * Frame format: [canId_lo, canId_hi, data_byte_0, ...]
     * Returns 0 if frame is too short (< 2 bytes).
     *
     * @param frame Raw CAN frame
     * @return CAN ID (16-bit, little-endian)
     */
    [[nodiscard]] static uint16_t extractCANId(
        const std::vector<uint8_t>& frame
    ) noexcept;

    /**
     * Decode CAN 280 (0x118) DI_systemStatus.
     *
     * Shared decoder for accelerator pedal + brake state.
     * Used by both Tesla (model3dbc) and Audi (vw_mlb.dbc).
     *
     * @param data 8-byte CAN data payload
     * @return pair<acceleratorPercent, brakeEnumValue>
     *   brakeEnumValue: raw DBC enum (0.0=off, 1.0-3.0=on)
     *   Semantic mapping (e.g. 50%) is the translator's responsibility
     */
    [[nodiscard]] static std::pair<double, double> decodeCAN280(
        const std::vector<uint8_t>& data
    ) noexcept;

    /**
     * Decode CAN 297 (0x129) SCCM_steeringAngleSensor.
     *
     * Shared decoder for steering angle.
     * Used by both Tesla (model3dbc) and Audi (vw_mlb.dbc).
     *
     * @param data 8-byte CAN data payload
     * @return steering angle in degrees (clamped to -819.2..+819.2)
     */
    [[nodiscard]] static double decodeCAN297(
        const std::vector<uint8_t>& data
    ) noexcept;

    /**
     * Build VehicleSignal from accumulated state.
     *
     * Always returns a VehicleSignal with current accumulated state
     * and timestamp from the injected ITimeProvider.
     * Constructor precondition ensures timeProvider is non-null.
     */
    [[nodiscard]] VehicleSignal buildSignal() const noexcept;

    // Protected getters for reading accumulated state in decoder lambdas
    [[nodiscard]] double getSpeedKmh() const noexcept;
    [[nodiscard]] double getThrottlePercent() const noexcept;
    [[nodiscard]] double getAccelerationG() const noexcept;
    [[nodiscard]] double getBrakePercent() const noexcept;
    [[nodiscard]] double getSteeringAngleDeg() const noexcept;
    [[nodiscard]] double getMotorRpm() const noexcept;
    [[nodiscard]] double getMotorHvVoltage() const noexcept;
    [[nodiscard]] double getMotorHvCurrent() const noexcept;
    [[nodiscard]] double getMotorPower() const noexcept;
    [[nodiscard]] double getRegenPower() const noexcept;

    // Protected setters with clamping invariants
    void setSpeedKmh(double v) const noexcept;
    void setThrottlePercent(double v) const noexcept;
    void setAccelerationG(double v) const noexcept;
    void setBrakePercent(double v) const noexcept;
    void setSteeringAngleDeg(double v) const noexcept;
    void setMotorRpm(double v) const noexcept;
    void setMotorHvVoltage(double v) const noexcept;
    void setMotorHvCurrent(double v) const noexcept;
    void setMotorPower(double v) const noexcept;
    void setRegenPower(double v) const noexcept;

private:
    std::unique_ptr<ITimeProvider> timeProvider_;
    DecoderMap decoders_;

    // Accumulated state (private, accessed via protected setters)
    mutable double lastSpeedKmh_ = 0.0;
    mutable double lastThrottlePercent_ = 0.0;
    mutable double lastAccelerationG_ = 0.0;
    mutable double lastBrakePercent_ = 0.0;
    mutable double lastSteeringAngleDeg_ = 0.0;
    mutable double lastMotorRpm_ = 0.0;
    mutable double lastMotorHvVoltage_ = 0.0;
    mutable double lastMotorHvCurrent_ = 0.0;
    mutable double lastMotorPower_ = 0.0;
    mutable double lastRegenPower_ = 0.0;
};

} // namespace vehicle_sim::domain
