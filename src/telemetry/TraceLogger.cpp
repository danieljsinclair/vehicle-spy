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

TraceLogger::~TraceLogger() {
    if (file_.is_open()) {
        file_.close();
    }
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

TraceLogger::TraceLogger(TraceLogger&& other) noexcept
    : file_(std::move(other.file_))
    , vehicleId_(std::move(other.vehicleId_)) {}

TraceLogger& TraceLogger::operator=(TraceLogger&& other) noexcept {
    if (this != &other) {
        if (file_.is_open()) {
            file_.close();
        }
        file_ = std::move(other.file_);
        vehicleId_ = std::move(other.vehicleId_);
    }
    return *this;
}

}
