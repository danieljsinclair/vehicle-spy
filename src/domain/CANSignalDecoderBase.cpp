#include "vehicle-sim/domain/CANSignalDecoderBase.h"
#include "vehicle-sim/domain/CANDecoder.h"

#include <algorithm>
#include <cassert>

namespace vehicle_sim::domain {

CANSignalDecoderBase::CANSignalDecoderBase(
    std::unique_ptr<ITimeProvider> timeProvider,
    DecoderMap decoders
)
    : timeProvider_(std::move(timeProvider))
    , decoders_(std::move(decoders))
{
    assert(timeProvider_ != nullptr);
}

bool CANSignalDecoderBase::isValidFrame(
    const std::vector<uint8_t>& frame
) const noexcept {
    const uint16_t id = extractCANId(frame);
    if (id == 0 && frame.size() < 2) {
        return false;
    }
    return decoders_.find(id) != decoders_.end();
}

std::optional<VehicleSignal> CANSignalDecoderBase::translateFrame(
    const std::vector<uint8_t>& frame
) const noexcept {
    if (frame.size() < CAN_FRAME_SIZE) {
        return std::nullopt;
    }

    const uint16_t id = extractCANId(frame);
    auto it = decoders_.find(id);
    if (it == decoders_.end()) {
        return std::nullopt;
    }

    std::vector<uint8_t> data(
        frame.begin() + CAN_DATA_OFFSET,
        frame.begin() + CAN_DATA_OFFSET + (CAN_FRAME_SIZE - CAN_DATA_OFFSET)
    );

    it->second(data);

    return buildSignal();
}

uint16_t CANSignalDecoderBase::extractCANId(
    const std::vector<uint8_t>& frame
) noexcept {
    if (frame.size() < 2) return 0;
    return static_cast<uint16_t>(frame[0]) |
           (static_cast<uint16_t>(frame[1]) << 8);
}

std::pair<double, double> CANSignalDecoderBase::decodeCAN280(
    const std::vector<uint8_t>& data
) noexcept {
    auto pedal = CANDecoder::extractSignal(data, 32, 8, 0.4, 0.0, false);
    double accel = pedal.has_value() ? std::clamp(*pedal, 0.0, 100.0) : 0.0;

    auto brakeState = CANDecoder::extractSignal(data, 19, 2, 1.0, 0.0, false);
    double brake = brakeState.has_value() ? *brakeState : 0.0;

    return {accel, brake};
}

double CANSignalDecoderBase::decodeCAN297(
    const std::vector<uint8_t>& data
) noexcept {
    auto angle = CANDecoder::extractSignal(data, 16, 14, 0.1, -819.2, false);
    if (angle.has_value()) {
        return std::clamp(*angle, -819.2, 819.2);
    }
    return 0.0;
}

VehicleSignal CANSignalDecoderBase::buildSignal() const noexcept {
    return VehicleSignal(
        timeProvider_->nowMs(),
        lastThrottlePercent_,
        lastSpeedKmh_,
        lastAccelerationG_,
        lastBrakePercent_,
        lastSteeringAngleDeg_,
        lastMotorRpm_,
        lastMotorHvVoltage_,
        lastMotorHvCurrent_,
        lastMotorTorqueNm_,
        std::nullopt  // gearSelector
    );
}

// --- Getters ---

double CANSignalDecoderBase::getSpeedKmh() const noexcept { return lastSpeedKmh_; }
double CANSignalDecoderBase::getThrottlePercent() const noexcept { return lastThrottlePercent_; }
double CANSignalDecoderBase::getAccelerationG() const noexcept { return lastAccelerationG_; }
double CANSignalDecoderBase::getBrakePercent() const noexcept { return lastBrakePercent_; }
double CANSignalDecoderBase::getSteeringAngleDeg() const noexcept { return lastSteeringAngleDeg_; }
double CANSignalDecoderBase::getMotorRpm() const noexcept { return lastMotorRpm_; }
double CANSignalDecoderBase::getMotorHvVoltage() const noexcept { return lastMotorHvVoltage_; }
double CANSignalDecoderBase::getMotorHvCurrent() const noexcept { return lastMotorHvCurrent_; }
double CANSignalDecoderBase::getMotorTorqueNm() const noexcept { return lastMotorTorqueNm_; }

// --- Setters with clamping ---

void CANSignalDecoderBase::setSpeedKmh(double v) const noexcept {
    lastSpeedKmh_ = std::clamp(v, VehicleSignal::SPEED_MIN, VehicleSignal::SPEED_MAX);
}

void CANSignalDecoderBase::setThrottlePercent(double v) const noexcept {
    lastThrottlePercent_ = std::clamp(v, VehicleSignal::THROTTLE_MIN, VehicleSignal::THROTTLE_MAX);
}

void CANSignalDecoderBase::setAccelerationG(double v) const noexcept {
    lastAccelerationG_ = std::clamp(v, VehicleSignal::ACCEL_MIN, VehicleSignal::ACCEL_MAX);
}

void CANSignalDecoderBase::setBrakePercent(double v) const noexcept {
    lastBrakePercent_ = std::clamp(v, VehicleSignal::BRAKE_MIN, VehicleSignal::BRAKE_MAX);
}

void CANSignalDecoderBase::setSteeringAngleDeg(double v) const noexcept {
    lastSteeringAngleDeg_ = std::clamp(v, VehicleSignal::STEERING_MIN, VehicleSignal::STEERING_MAX);
}

void CANSignalDecoderBase::setMotorRpm(double v) const noexcept {
    lastMotorRpm_ = std::clamp(v, VehicleSignal::MOTOR_RPM_MIN, VehicleSignal::MOTOR_RPM_MAX);
}

void CANSignalDecoderBase::setMotorHvVoltage(double v) const noexcept {
    lastMotorHvVoltage_ = std::clamp(v, VehicleSignal::HV_VOLTAGE_MIN, VehicleSignal::HV_VOLTAGE_MAX);
}

void CANSignalDecoderBase::setMotorHvCurrent(double v) const noexcept {
    lastMotorHvCurrent_ = std::clamp(v, VehicleSignal::HV_CURRENT_MIN, VehicleSignal::HV_CURRENT_MAX);
}

void CANSignalDecoderBase::setMotorTorqueNm(double v) const noexcept {
    lastMotorTorqueNm_ = std::clamp(v, VehicleSignal::MOTOR_TORQUE_MIN, VehicleSignal::MOTOR_TORQUE_MAX);
}

} // namespace vehicle_sim::domain
