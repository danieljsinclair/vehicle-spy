#include "vehicle-sim/domain/VehicleSignalFactory.h"
#include "vehicle-sim/domain/DBCSignalMapper.h"

namespace vehicle_sim::domain {

VehicleSignalFactory::VehicleSignalFactory(
    const VehicleConfig& config,
    const DBCParseResult& parseResult
) noexcept
    : config_(config)
    , parseResult_(parseResult)
{
}

VehicleSignal VehicleSignalFactory::build(
    const std::unordered_map<std::uint16_t, std::vector<std::uint8_t>>& frames,
    std::uint64_t timestampUtcMs
) const noexcept {
    double throttlePercent = 0.0;
    double speedKmh = 0.0;
    double accelerationG = 0.0;
    double brakePercent = 0.0;
    double steeringAngleDeg = 0.0;
    double motorRpm = 0.0;
    double motorHvVoltage = 0.0;
    double motorHvCurrent = 0.0;
    double motorPower = 0.0;
    double regenPower = 0.0;
    double motorTorqueNm = 0.0;

    for (const auto& [signalName, fieldName] : config_.signalMappings) {
        double* targetField = nullptr;

        if (fieldName == "throttlePercent") targetField = &throttlePercent;
        else if (fieldName == "speedKmh") targetField = &speedKmh;
        else if (fieldName == "accelerationG") targetField = &accelerationG;
        else if (fieldName == "brakePercent") targetField = &brakePercent;
        else if (fieldName == "steeringAngleDeg") targetField = &steeringAngleDeg;
        else if (fieldName == "motorRpm") targetField = &motorRpm;
        else if (fieldName == "motorHvVoltage") targetField = &motorHvVoltage;
        else if (fieldName == "motorHvCurrent") targetField = &motorHvCurrent;
        else if (fieldName == "motorPower") targetField = &motorPower;
        else if (fieldName == "regenPower") targetField = &regenPower;
        else if (fieldName == "motorTorqueNm") targetField = &motorTorqueNm;

        if (!targetField) continue;

        for (const auto& [canId, frame] : frames) {
            auto value = DBCSignalMapper::mapSignal(
                frame,
                canId,
                signalName,
                parseResult_.signalsByCanId
            );
            if (value) {
                *targetField = *value;
                break;
            }
        }
    }

    return VehicleSignal(
        throttlePercent,
        speedKmh,
        accelerationG,
        brakePercent,
        timestampUtcMs,
        steeringAngleDeg,
        motorRpm,
        motorHvVoltage,
        motorHvCurrent,
        motorPower,
        regenPower,
        motorTorqueNm
    );
}

} // namespace vehicle_sim::domain
