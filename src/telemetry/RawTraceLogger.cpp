#include "vehicle-sim/telemetry/RawTraceLogger.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <iomanip>
#include <sstream>

namespace vehicle_sim::telemetry {

RawTraceLogger::RawTraceLogger(std::string filePath)
    : file_(filePath, std::ios::app)
{
    if (!file_) {
        throw domain::TelemetryFileException(filePath);
    }
    file_ << "# vehicle-sim raw CAN capture\n";
    file_ << "# format: timestamp_utc_ms,data_hex\n";
    file_.flush();
}

RawTraceLogger::~RawTraceLogger() {
    if (file_.is_open()) {
        file_.close();
    }
}

void RawTraceLogger::write(std::uint64_t timestampMs, const std::vector<std::uint8_t>& data) noexcept {
    if (!file_.is_open()) {
        return;
    }

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (std::uint8_t byte : data) {
        hex << std::setw(2) << static_cast<int>(byte);
    }

    file_ << timestampMs << "," << hex.str() << "\n";
    file_.flush();
}

bool RawTraceLogger::isValid() const noexcept {
    return file_.is_open();
}

RawTraceLogger::RawTraceLogger(RawTraceLogger&& other) noexcept
    : file_(std::move(other.file_)) {}

RawTraceLogger& RawTraceLogger::operator=(RawTraceLogger&& other) noexcept {
    if (this != &other) {
        if (file_.is_open()) {
            file_.close();
        }
        file_ = std::move(other.file_);
    }
    return *this;
}

} // namespace vehicle_sim::telemetry