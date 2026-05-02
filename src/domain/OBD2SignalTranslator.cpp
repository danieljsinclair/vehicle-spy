#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/OBD2SignalTranslatorBase.h"
#include "vehicle-sim/domain/OBD2Math.h"

namespace vehicle_sim::domain {

OBD2SignalTranslator::OBD2SignalTranslator() = default;

double OBD2SignalTranslator::extractPIDValue(
    std::uint8_t pid,
    const std::vector<std::uint8_t>& data
) const noexcept {
    if (data.empty()) {
        return 0.0;
    }

    switch (pid) {
        case PID_VEHICLE_SPEED:
            return obd2RawValue(data[0]);

        case PID_THROTTLE_POSITION:
            return obd2BytePercent(data[0]);

        case PID_ENGINE_LOAD:
            return obd2BytePercent(data[0]);

        case PID_ENGINE_RPM:
            if (data.size() >= 2) {
                return obd2WordRPM(data[0], data[1]);
            }
            return obd2RawValue(data[0]);

        case PID_COOLANT_TEMP:
            return obd2TempCelsius(data[0]);

        case PID_FUEL_LEVEL:
            return obd2BytePercent(data[0]);

        case PID_ACCELERATOR_POS_D:
            return obd2BytePercent(data[0]);

        default:
            return obd2RawValue(data[0]);
    }
}

} // namespace vehicle_sim::domain
