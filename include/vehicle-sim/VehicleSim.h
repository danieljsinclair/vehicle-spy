#pragma once

#include <memory>
#include <string>
#include <functional>
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim {

class VehicleSimulator {
public:
    VehicleSimulator();
    ~VehicleSimulator();

    // Initialize the simulator with configuration
    bool initialize(const std::string& config_file = "") const;

    // Start data capture and processing
    bool start();

    // Stop all capture and processing
    void stop();

    // Main update loop - animates simulation values
    void update();

    // Get the latest typed signal (single source of truth)
    [[nodiscard]] domain::VehicleSignal getLatestSignal() const;

    // Get current telemetry data as JSON (CLI backward compat)
    std::string getTelemetry() const;

    // Set callback for event-driven architecture
    using TelemetryCallback = std::function<void(const std::string& data)>;
    void setTelemetryCallback(TelemetryCallback callback);

    // Non-blocking query interface
    bool hasNewData() const;
    std::string getLatestTelemetry() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace vehicle_sim
