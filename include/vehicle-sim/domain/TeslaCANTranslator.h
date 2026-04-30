#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace vehicle_sim::domain {

/**
 * CAN frame decoder for Tesla Model 3/Y.
 *
 * Decodes raw CAN frames using verified signal definitions from
 * joshwardell/model3dbc.
 *
 * Frame format expected by translate():
 *   [canId_lo, canId_hi, data_byte_0, ..., data_byte_7]
 *
 * Supported CAN IDs (verified from model3dbc):
 *   280 (0x118) DI_systemStatus — accelerator, brake, gear
 *   297 (0x129) SCCM_steeringAngleSensor — steering angle
 *
 * NOTE: Additional Tesla-specific CAN IDs (e.g., UI_gpsVehicleSpeed CAN 985,
 * RCM_inertial2 CAN 273) will be added once exact DBC signal bit positions
 * are verified against the model3dbc file.
 */
class TeslaCANTranslator final : public ISignalTranslator {
public:
    TeslaCANTranslator();
    ~TeslaCANTranslator() override = default;

    TeslaCANTranslator(const TeslaCANTranslator&) = delete;
    TeslaCANTranslator& operator=(const TeslaCANTranslator&) = delete;

    [[nodiscard]] bool isValidPacket(
        const std::vector<uint8_t>& rawData
    ) const noexcept override;

    [[nodiscard]] std::optional<VehicleSignal> translate(
        const std::vector<uint8_t>& rawData
    ) const noexcept override;

private:
    static constexpr std::size_t CAN_DATA_OFFSET = 2;
    static constexpr std::size_t CAN_FRAME_SIZE = 10;

    // CAN IDs from model3dbc
    static constexpr uint16_t CAN_ID_DI_SYSTEM = 280;
    static constexpr uint16_t CAN_ID_SCCM_STEER = 297;

    // DI_systemStatus signal: DI_accelPedalPos (shared with Audi MLB)
    static constexpr std::size_t DI_PEDAL_START_BIT = 32;
    static constexpr std::size_t DI_PEDAL_BIT_LENGTH = 8;
    static constexpr double DI_PEDAL_SCALE = 0.4;            // % per count

    // DI_systemStatus signal: DI_brakePedalState (shared with Audi MLB)
    static constexpr std::size_t DI_BRAKE_STATE_START_BIT = 17;
    static constexpr std::size_t DI_BRAKE_STATE_BIT_LENGTH = 2;
    static constexpr double BRAKE_PEDAL_PRESSED_PERCENT = 50.0;  // binary state → 50%

    // SCCM_steeringAngleSensor signal: SCCM_steeringAngle (shared with Audi MLB)
    static constexpr std::size_t SCCM_STEER_START_BIT = 16;
    static constexpr std::size_t SCCM_STEER_BIT_LENGTH = 14;
    static constexpr double SCCM_STEER_SCALE = 0.1;          // deg per count
    static constexpr double SCCM_STEER_OFFSET = -819.2;      // deg

    [[nodiscard]] static uint16_t extractCANId(
        const std::vector<uint8_t>& frame
    ) noexcept;

    void decodeDISystem(const std::vector<uint8_t>& data) const;
    void decodeSCCMSteering(const std::vector<uint8_t>& data) const;

    [[nodiscard]] std::optional<VehicleSignal> buildSignal() const noexcept;

    mutable double lastSpeedKmh_ = 0.0;
    mutable double lastThrottlePercent_ = 0.0;
    mutable double lastAccelerationG_ = 0.0;
    mutable double lastBrakePercent_ = 0.0;
    mutable double lastSteeringAngleDeg_ = 0.0;
};

} // namespace vehicle_sim::domain
