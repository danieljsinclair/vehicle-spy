#pragma once

#include <memory>
#include <string>
#include <functional>

namespace vehicle_sim {

class VehicleSimulator {
public:
    VehicleSimulator();
    ~VehicleSimulator();

    // Initialize the simulator with configuration
    bool initialize(const std::string& config_file = "");

    // Start data capture and processing
    bool start();

    // Stop all capture and processing
    void stop();

    // Main update loop - call regularly for real-time processing
    void update();

    // Get current telemetry data
    // Returns JSON or structured data depending on configuration
    std::string getTelemetry() const;

    // Set callback for event-driven architecture
    using TelemetryCallback = std::function<void(const std::string& data)>;
    void setTelemetryCallback(TelemetryCallback callback);

    // Non-blocking query interface
    bool hasNewData() const;
    std::string getLatestTelemetry();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace vehicle_sim
