#pragma once

#include <string>
#include <fstream>
#include <optional>
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/telemetry/VehicleId.h"

namespace vehicle_sim::telemetry {

class TraceLogger {
public:
    explicit TraceLogger(const std::string& filePath, const std::string& vehicleId = "");

    // rule of zero: std::ofstream owns the file; the compiler-generated special
    // members (move/copy + dtor) are correct and noexcept where needed.

    void operator()(const domain::VehicleSignal& signal) noexcept;

    [[nodiscard]] bool isValid() const noexcept;

private:
    void writeHeader();
    void writeRow(const domain::VehicleSignal& signal);

    std::ofstream file_;
    VehicleId vehicleId_;
};

}