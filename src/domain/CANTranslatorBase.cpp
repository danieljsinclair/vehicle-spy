#include "vehicle-sim/domain/CANTranslatorBase.h"
#include "vehicle-sim/domain/CANDecoder.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include <memory>

namespace vehicle_sim::domain {

CANTranslatorBase::CANTranslatorBase(std::shared_ptr<ITimeProvider> timeProvider) {
    if (timeProvider) {
        timeProvider_ = std::move(timeProvider);
    } else {
        timeProvider_ = std::make_shared<SystemTimeProvider>();
    }
}

CANTranslatorBase::~CANTranslatorBase() = default;

bool CANTranslatorBase::isValidPacket(
    const std::vector<uint8_t>& rawData
) const noexcept {
    if (rawData.size() < CAN_FRAME_SIZE) {
        return false;
    }
    const uint16_t id = extractCANId(rawData);
    return isKnownCANId(id);
}

std::optional<VehicleSignal> CANTranslatorBase::translate(
    const std::vector<uint8_t>& rawData
) const noexcept {
    if (rawData.size() < CAN_FRAME_SIZE) {
        return std::nullopt;
    }

    const uint16_t id = extractCANId(rawData);
    if (!isKnownCANId(id)) {
        return std::nullopt;
    }

    std::vector<uint8_t> data(
        rawData.begin() + CAN_DATA_OFFSET,
        rawData.begin() + CAN_FRAME_SIZE
    );

    decodeFrame(id, data);
    return buildSignal();
}

uint16_t CANTranslatorBase::extractCANId(
    const std::vector<uint8_t>& frame
) noexcept {
    if (frame.size() < 2) return 0;
    return static_cast<uint16_t>(frame[0]) |
           (static_cast<uint16_t>(frame[1]) << 8);
}

void CANTranslatorBase::decodeDISystem(const std::vector<uint8_t>& data) const {
    auto pedal = CANDecoder::extractSignal(data, DI_PEDAL_START_BIT, DI_PEDAL_BIT_LENGTH, DI_PEDAL_SCALE, 0.0, false);
    if (pedal.has_value()) {
        lastThrottlePercent_ = *pedal;
    }

    auto brakeState = CANDecoder::extractSignal(data, DI_BRAKE_STATE_START_BIT, DI_BRAKE_STATE_BIT_LENGTH, 1.0, 0.0, false);
    if (brakeState.has_value() && *brakeState > 0) {
        lastBrakePercent_ = std::max(lastBrakePercent_, BRAKE_PEDAL_PRESSED_PERCENT);
    }
}

void CANTranslatorBase::decodeSCCMSteering(const std::vector<uint8_t>& data) const {
    auto angle = CANDecoder::extractSignal(data, SCCM_STEER_START_BIT, SCCM_STEER_BIT_LENGTH, SCCM_STEER_SCALE, SCCM_STEER_OFFSET, false);
    if (angle.has_value()) {
        lastSteeringAngleDeg_ = *angle;
    }
}

std::optional<VehicleSignal> CANTranslatorBase::buildSignal() const noexcept {
    return VehicleSignal(timeProvider_->nowMs(), lastThrottlePercent_, lastSpeedKmh_, lastAccelerationG_, lastBrakePercent_, lastSteeringAngleDeg_);
}

} // namespace vehicle_sim::domain
