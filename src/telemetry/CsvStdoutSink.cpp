#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/domain/Gear.h"

#include <optional>
#include <ostream>
#include <utility>

namespace vehicle_sim::telemetry {

CsvStdoutSink::CsvStdoutSink(std::ostream& out, const std::string& vehicleId)
    : out_(out)
    // The operator-supplied --vehicle label is converted to the validated
    // VehicleId type here: forLog sanitizes control bytes at this boundary.
    , vehicleId_(VehicleId::fromUserInput(vehicleId))
{
    writeHeader();
}

void CsvStdoutSink::operator()(const domain::VehicleSignal& signal) noexcept {
    writeRow(signal);
}

void CsvStdoutSink::writeHeader() {
    out_ << csvHeaderLine() << "\n";
    out_.flush();
}

void CsvStdoutSink::writeRow(const domain::VehicleSignal& signal) {
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

    out_ << csvRowLine(params) << "\n";
    out_.flush();
}

void NullCsvStdoutSink::operator()(const domain::VehicleSignal& /*signal*/) noexcept {
    // Intentionally no-op — the disabled arm of the --stdout-csv branch.
}

std::unique_ptr<ICsvStdoutSink> createStdoutSink(bool enabled,
                                                 std::ostream& out,
                                                 const std::string& vehicleId) {
    std::unique_ptr<ICsvStdoutSink> sink;
    if (enabled) {
        sink = std::make_unique<CsvStdoutSink>(out, vehicleId);
    } else {
        sink = std::make_unique<NullCsvStdoutSink>();
    }
    return sink;
}

} // namespace vehicle_sim::telemetry
