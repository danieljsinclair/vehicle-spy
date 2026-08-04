#include "vehicle-sim/pipeline/CsvStdoutReporter.h"

namespace vehicle_sim::pipeline {

CsvStdoutReporter::CsvStdoutReporter(telemetry::ICsvStdoutSink& sink) noexcept
    : sink_(sink)
{
}

void CsvStdoutReporter::onFrame(
    const domain::VehicleSignal& signal,
    std::size_t /*frameIndex*/,
    std::size_t /*totalHints*/
) noexcept {
    sink_(signal);
}

void CsvStdoutReporter::onComplete(const ReplayStats& /*stats*/) noexcept {
    // No trailing summary: a CSV stream ends at its last data row.
}

} // namespace vehicle_sim::pipeline
