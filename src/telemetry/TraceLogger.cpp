#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"


namespace vehicle_sim::telemetry {

TraceLogger::TraceLogger(const std::string& filePath, std::string vehicleId)
    : file_(filePath)
    , vehicleId_(std::move(vehicleId))
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
    // Schema owned by CsvRowFormatter so the file writer and the stdout
    // writer (CsvStdoutSink) cannot drift apart.
    file_ << csvHeaderLine() << "\n";
    file_.flush();
}

void TraceLogger::writeRow(const domain::VehicleSignal& signal) {
    file_ << csvRowLine(signal, vehicleId_) << "\n";
    file_.flush();
}

}
