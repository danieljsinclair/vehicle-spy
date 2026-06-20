#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/telemetry/TraceLogger.h"

namespace vehicle_sim::pipeline {

DecodedCsvSink::DecodedCsvSink(const std::string& base, const std::string& vehicleId)
    // TraceLogger opens the file and writes the header; throws on failure.
    : logger_(std::make_unique<telemetry::TraceLogger>(base + ".csv", vehicleId)) {}

DecodedCsvSink::~DecodedCsvSink() = default;

DecodedCsvSink::DecodedCsvSink(DecodedCsvSink&&) noexcept = default;
DecodedCsvSink& DecodedCsvSink::operator=(DecodedCsvSink&&) noexcept = default;

void DecodedCsvSink::write(const domain::VehicleSignal& signal) noexcept {
    if (logger_) {
        (*logger_)(signal);
    }
}

bool DecodedCsvSink::isValid() const noexcept {
    return logger_ && logger_->isValid();
}

} // namespace vehicle_sim::pipeline
