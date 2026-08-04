#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include <ostream>
#include <utility>

namespace vehicle_sim::telemetry {

CsvStdoutSink::CsvStdoutSink(std::ostream& out, std::string vehicleId)
    : out_(out)
    , vehicleId_(std::move(vehicleId))
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
    // Flushed per row: stdout is typically a pipe here, and a downstream
    // consumer must see rows as they are decoded rather than in 4 KiB bursts.
    out_ << csvRowLine(signal, vehicleId_) << "\n";
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
