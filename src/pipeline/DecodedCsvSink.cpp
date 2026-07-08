#include "vehicle-sim/pipeline/DecodedCsvSink.h"

namespace vehicle_sim::pipeline {

DecodedCsvSink::DecodedCsvSink(const std::string& base, const std::string& vehicleId)
    // TraceLogger opens the file and writes the header; throws on failure.
    : logger_(base + ".csv", vehicleId) {}

void DecodedCsvSink::write(const domain::VehicleSignal& signal) noexcept {
    logger_(signal);
}

bool DecodedCsvSink::isValid() const noexcept {
    return logger_.isValid();
}

} // namespace vehicle_sim::pipeline
