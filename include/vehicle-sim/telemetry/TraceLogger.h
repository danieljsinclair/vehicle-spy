#pragma once

#include <string>
#include <fstream>
#include <optional>
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::telemetry {

class TraceLogger {
public:
    explicit TraceLogger(std::string filePath, std::string vehicleId = "");
    ~TraceLogger();

    void operator()(const domain::VehicleSignal& signal) noexcept;

    TraceLogger(const TraceLogger&) = delete;
    TraceLogger& operator=(const TraceLogger&) = delete;
    TraceLogger(TraceLogger&&) noexcept;
    TraceLogger& operator=(TraceLogger&&) noexcept;

    [[nodiscard]] bool isValid() const noexcept;

private:
    void writeHeader();
    void writeRow(const domain::VehicleSignal& signal);
    std::string formatOptional(std::optional<double> value);
    std::string formatOptional(std::optional<std::int32_t> value);

    std::ofstream file_;
    std::string vehicleId_;
};

}

