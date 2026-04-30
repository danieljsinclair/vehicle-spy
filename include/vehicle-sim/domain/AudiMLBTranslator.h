#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace vehicle_sim::domain {

/**
 * CAN frame decoder for Audi e-tron 2021 (MLB Evo platform).
 *
 * Decodes raw CAN frames using verified signal definitions from
 * commaai/opendbc vw_mlb.dbc. Each CAN ID maps to a specific
 * message containing one or more signals.
 *
 * Frame format expected by translate():
 *   [canId_lo, canId_hi, data_byte_0, ..., data_byte_7]
 *
 * Supported CAN IDs:
 *   256 (0x100) ESP_01   — vehicle speed
 *   257 (0x101) ESP_02   — longitudinal/lateral acceleration
 *   259 (0x103) ESP_03   — wheel speeds (4 corners)
 *   262 (0x106) ESP_05   — brake pressure
 *   280 (0x118) DI_systemStatus — accelerator pedal, brake state
 *   297 (0x129) SCCM_steeringAngleSensor — steering angle
 */
class AudiMLBTranslator final : public ISignalTranslator {
public:
    AudiMLBTranslator();
    ~AudiMLBTranslator() override = default;

    AudiMLBTranslator(const AudiMLBTranslator&) = delete;
    AudiMLBTranslator& operator=(const AudiMLBTranslator&) = delete;

    [[nodiscard]] bool isValidPacket(
        const std::vector<uint8_t>& rawData
    ) const noexcept override;

    [[nodiscard]] std::optional<VehicleSignal> translate(
        const std::vector<uint8_t>& rawData
    ) const noexcept override;

private:
    static constexpr std::size_t CAN_DATA_OFFSET = 2;  // 2 bytes for CAN ID
    static constexpr std::size_t CAN_FRAME_SIZE = 10;   // 2 ID + 8 data

    // CAN IDs from vw_mlb.dbc
    static constexpr uint16_t CAN_ID_ESP_01 = 256;
    static constexpr uint16_t CAN_ID_ESP_02 = 257;
    static constexpr uint16_t CAN_ID_ESP_03 = 259;
    static constexpr uint16_t CAN_ID_ESP_05 = 262;
    static constexpr uint16_t CAN_ID_DI_SYSTEM = 280;
    static constexpr uint16_t CAN_ID_SCCM_STEER = 297;

    // ESP_01 signal: ESP_v_Signal (vehicle speed)
    static constexpr std::size_t ESP01_SPEED_START_BIT = 32;
    static constexpr std::size_t ESP01_SPEED_BIT_LENGTH = 16;
    static constexpr double ESP01_SPEED_SCALE = 0.01;        // km/h per count

    // ESP_02 signal: ESP_Laengsbeschl (longitudinal acceleration)
    static constexpr std::size_t ESP02_ACCEL_START_BIT = 24;
    static constexpr std::size_t ESP02_ACCEL_BIT_LENGTH = 10;
    static constexpr double ESP02_ACCEL_SCALE = 0.03125;     // m/s^2 per count
    static constexpr double ESP02_ACCEL_OFFSET = -16.0;      // m/s^2
    static constexpr double MS2_TO_G = 9.81;                 // m/s^2 per g

    // ESP_05 signal: ESP_Bremsdruck (brake pressure)
    static constexpr std::size_t ESP05_BRAKE_START_BIT = 16;
    static constexpr std::size_t ESP05_BRAKE_BIT_LENGTH = 10;
    static constexpr double ESP05_BRAKE_SCALE = 0.3;         // bar per count
    static constexpr double ESP05_BRAKE_OFFSET = -30.0;      // bar
    static constexpr double BRAKE_PRESSURE_MAX_BAR = 200.0;  // 0 bar = 0%, 200 bar = 100%

    // DI_systemStatus signal: DI_accelPedalPos (shared with Tesla)
    static constexpr std::size_t DI_PEDAL_START_BIT = 32;
    static constexpr std::size_t DI_PEDAL_BIT_LENGTH = 8;
    static constexpr double DI_PEDAL_SCALE = 0.4;            // % per count

    // DI_systemStatus signal: DI_brakePedalState (shared with Tesla)
    static constexpr std::size_t DI_BRAKE_STATE_START_BIT = 17;
    static constexpr std::size_t DI_BRAKE_STATE_BIT_LENGTH = 2;
    static constexpr double BRAKE_PEDAL_PRESSED_PERCENT = 50.0;  // binary state → 50%

    // SCCM_steeringAngleSensor signal: SCCM_steeringAngle (shared with Tesla)
    static constexpr std::size_t SCCM_STEER_START_BIT = 16;
    static constexpr std::size_t SCCM_STEER_BIT_LENGTH = 14;
    static constexpr double SCCM_STEER_SCALE = 0.1;          // deg per count
    static constexpr double SCCM_STEER_OFFSET = -819.2;      // deg

    [[nodiscard]] static uint16_t extractCANId(
        const std::vector<uint8_t>& frame
    ) noexcept;

    void decodeESP01(const std::vector<uint8_t>& data) const;
    void decodeESP02(const std::vector<uint8_t>& data) const;
    void decodeESP03(const std::vector<uint8_t>& data) const;
    void decodeESP05(const std::vector<uint8_t>& data) const;
    void decodeDISystem(const std::vector<uint8_t>& data) const;
    void decodeSCCMSteering(const std::vector<uint8_t>& data) const;

    [[nodiscard]] std::optional<VehicleSignal> buildSignal() const noexcept;

    // Accumulated state across CAN frames
    mutable double lastSpeedKmh_ = 0.0;
    mutable double lastThrottlePercent_ = 0.0;
    mutable double lastAccelerationG_ = 0.0;
    mutable double lastBrakePercent_ = 0.0;
    mutable double lastSteeringAngleDeg_ = 0.0;
};

} // namespace vehicle_sim::domain
