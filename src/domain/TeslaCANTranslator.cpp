#include "vehicle-sim/domain/TeslaCANTranslator.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/CANDecoder.h"

#include <chrono>
#include <algorithm>

namespace vehicle_sim::domain {

TeslaCANTranslator::TeslaCANTranslator() = default;

bool TeslaCANTranslator::isValidPacket(
    const std::vector<uint8_t>& rawData
) const noexcept {
    if (rawData.size() < CAN_FRAME_SIZE) {
        return false;
    }
    const uint16_t id = extractCANId(rawData);
    return id == CAN_ID_DI_SYSTEM || id == CAN_ID_SCCM_STEER;
}

uint16_t TeslaCANTranslator::extractCANId(
    const std::vector<uint8_t>& frame
) noexcept {
    if (frame.size() < 2) return 0;
    return static_cast<uint16_t>(frame[0]) |
           (static_cast<uint16_t>(frame[1]) << 8);
}

std::optional<VehicleSignal> TeslaCANTranslator::translate(
    const std::vector<uint8_t>& rawData
) const noexcept {
    if (rawData.size() < CAN_FRAME_SIZE) {
        return std::nullopt;
    }

    const uint16_t id = extractCANId(rawData);
    std::vector<uint8_t> data(
        rawData.begin() + CAN_DATA_OFFSET,
        rawData.begin() + CAN_DATA_OFFSET + 8
    );

    switch (id) {
        case CAN_ID_DI_SYSTEM: decodeDISystem(data); break;
        case CAN_ID_SCCM_STEER: decodeSCCMSteering(data); break;
        default: return std::nullopt;
    }

    return buildSignal();
}

// CAN 280 (0x118) DI_systemStatus
// DI_accelPedalPos: start bit 32, 8-bit unsigned, scale 0.4, unit %
// DI_brakePedalState: start bit 17, 2-bit
void TeslaCANTranslator::decodeDISystem(const std::vector<uint8_t>& data) const {
    auto pedal = CANDecoder::extractSignal(data, DI_PEDAL_START_BIT, DI_PEDAL_BIT_LENGTH, DI_PEDAL_SCALE, 0.0, false);
    if (pedal.has_value()) {
        lastThrottlePercent_ = std::clamp(*pedal, VehicleSignal::THROTTLE_MIN, VehicleSignal::THROTTLE_MAX);
    }

    auto brakeState = CANDecoder::extractSignal(data, DI_BRAKE_STATE_START_BIT, DI_BRAKE_STATE_BIT_LENGTH, 1.0, 0.0, false);
    if (brakeState.has_value() && *brakeState > 0) {
        lastBrakePercent_ = std::max(lastBrakePercent_, BRAKE_PEDAL_PRESSED_PERCENT);
    }
}

// CAN 297 (0x129) SCCM_steeringAngleSensor
// SCCM_steeringAngle: start bit 16, 14-bit unsigned, scale 0.1, offset -819.2, unit deg
void TeslaCANTranslator::decodeSCCMSteering(const std::vector<uint8_t>& data) const {
    auto angle = CANDecoder::extractSignal(data, SCCM_STEER_START_BIT, SCCM_STEER_BIT_LENGTH, SCCM_STEER_SCALE, SCCM_STEER_OFFSET, false);
    if (angle.has_value()) {
        lastSteeringAngleDeg_ = std::clamp(*angle, VehicleSignal::STEERING_MIN, VehicleSignal::STEERING_MAX);
    }
}

std::optional<VehicleSignal> TeslaCANTranslator::buildSignal() const noexcept {
    auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    return VehicleSignal(
        lastThrottlePercent_,
        lastSpeedKmh_,
        lastAccelerationG_,
        lastBrakePercent_,
        now,
        lastSteeringAngleDeg_
    );
}

} // namespace vehicle_sim::domain
