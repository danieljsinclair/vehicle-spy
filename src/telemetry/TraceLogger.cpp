#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <optional>

namespace vehicle_sim::telemetry {

TraceLogger::TraceLogger(const std::string& filePath, const std::string& vehicleId)
    : file_(filePath)
    // The operator-supplied --vehicle label is converted to the validated
    // VehicleId type here: forLog sanitizes control bytes at this boundary.
    , vehicleId_(VehicleId::fromUserInput(vehicleId))
{
    if (!file_) {
        throw domain::TelemetryFileException(filePath);
    }
    writeHeader();
}

void TraceLogger::operator()(const domain::VehicleSignal& signal) noexcept {
    if (!file_.is_open()) {
        return;
    }
    writeRow(signal);
}

bool TraceLogger::isValid() const noexcept {
    return file_.is_open();
}

void TraceLogger::writeHeader() {
    file_ << csvHeaderLine() << "\n";
    file_.flush();
}

void TraceLogger::writeRow(const domain::VehicleSignal& signal) {
    // brake_light is a binary column: optional<bool> -> optional<int> (1/0/blank).
    std::optional<int> brakeLight;
    if (signal.getBrakeLight().has_value()) {
        brakeLight = *signal.getBrakeLight() ? 1 : 0;
    }

    // gear_selector renders as its display label (e.g. 4097 -> "D"), falling back
    // to the raw number for unmapped values — BUT only when the gear is reported.
    // A nullopt gear must render as an EMPTY cell (the "not reported" state),
    // distinct from a definite "N" (neutral). The label is a compile-time
    // constant or digit string — never external — so fromRegistry is correct.
    GearSelector gear;
    if (signal.getGearSelector().has_value()) {
        const auto gearLabel = domain::Gear::labelOr(
            *signal.getGearSelector(),
            std::to_string(*signal.getGearSelector()));
        gear = GearSelector::fromRegistry(gearLabel);
    }

    const CsvRowParams params{
        signal.getTimestampUtcMs(),
        vehicleId_,
        signal.getSpeedKmh(),
        signal.getThrottlePercent(),
        brakeLight,
        signal.getAccelerationG(),
        signal.getSteeringAngleDeg(),
        signal.getMotorRpm(),
        signal.getMotorHvVoltage(),
        signal.getMotorHvCurrent(),
        signal.getMotorTorqueNm(),
        gear,
        countPopulated(signal),
    };

    file_ << csvRowLine(params) << "\n";
    file_.flush();
}

} // namespace vehicle_sim::telemetry
