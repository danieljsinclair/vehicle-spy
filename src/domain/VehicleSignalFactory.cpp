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
    std::optional<double> throttlePercent;
    std::optional<double> speedKmh;
    std::optional<double> accelerationG;
    std::optional<double> brakePercent;
    std::optional<double> steeringAngleDeg;
    std::optional<double> motorRpm;
    std::optional<double> motorHvVoltage;
    std::optional<double> motorHvCurrent;
    std::optional<double> motorTorqueNm;
    std::optional<std::string> gearSelector;

    for (const auto& [signalName, fieldName] : config_.signalMappings) {
        std::optional<double>* targetField = nullptr;

        if (fieldName == "throttlePercent") targetField = &throttlePercent;
        else if (fieldName == "speedKmh") targetField = &speedKmh;
        else if (fieldName == "accelerationG") targetField = &accelerationG;
        else if (fieldName == "brakePercent") targetField = &brakePercent;
        else if (fieldName == "steeringAngleDeg") targetField = &steeringAngleDeg;
        else if (fieldName == "motorRpm") targetField = &motorRpm;
        else if (fieldName == "motorHvVoltage") targetField = &motorHvVoltage;
        else if (fieldName == "motorHvCurrent") targetField = &motorHvCurrent;
        else if (fieldName == "motorTorqueNm") targetField = &motorTorqueNm;

        if (!targetField) {
            if (fieldName == "gearSelector") {
                for (const auto& [canId, frame] : frames) {
                    auto value = DBCSignalMapper::mapSignal(
                        frame,
                        canId,
                        signalName,
                        parseResult_.signalsByCanId
                    );
                    if (value) {
                        int gearCode = static_cast<int>(*value);
                        auto it = config_.gearCodeMappings.find(gearCode);
                        if (it != config_.gearCodeMappings.end()) {
                            gearSelector = it->second;
                        }
                        break;
                    }
                }
            }
            continue;
        }

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
        timestampUtcMs,
        throttlePercent,
        speedKmh,
        accelerationG,
        brakePercent,
        steeringAngleDeg,
        motorRpm,
        motorHvVoltage,
        motorHvCurrent,
        motorTorqueNm,
        gearSelector
    );
}

} // namespace vehicle_sim::domain
