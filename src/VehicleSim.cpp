#include "vehicle-sim/VehicleSim.h"
#include <iostream>

namespace vehicle_sim {

// Define the PIMPL implementation
class VehicleSimulator::Impl {
public:
    Impl() : running_(false) {}
    ~Impl() = default;

    bool initialize(const std::string& config_file) {
        std::cout << "[VehicleSimulator] Initializing with config: " << (config_file.empty() ? "default" : config_file) << std::endl;
        return true;
    }

    bool start() {
        running_ = true;
        std::cout << "[VehicleSimulator] Started" << std::endl;
        return true;
    }

    void stop() {
        running_ = false;
        std::cout << "[VehicleSimulator] Stopped" << std::endl;
    }

    void update() {
        if (running_) {
            // Simulate processing - placeholder
        }
    }

    std::string getTelemetry() const {
        // Return mock telemetry as JSON
        return R"({"rpm":2500,"speed":85.0,"throttle":0.35,"brake":0.0,"gear":4,"torque":200.0,"accel":0.2})";
    }

    void setTelemetryCallback(TelemetryCallback callback) {
        callback_ = std::move(callback);
    }

    bool hasNewData() const {
        return running_;
    }

    std::string getLatestTelemetry() {
        return getTelemetry();
    }

private:
    bool running_;
    TelemetryCallback callback_;
};

// Public interface methods delegate to Impl
VehicleSimulator::VehicleSimulator() : pImpl(std::make_unique<Impl>()) {}
VehicleSimulator::~VehicleSimulator() = default;

bool VehicleSimulator::initialize(const std::string& config_file) {
    return pImpl->initialize(config_file);
}

bool VehicleSimulator::start() {
    return pImpl->start();
}

void VehicleSimulator::stop() {
    pImpl->stop();
}

void VehicleSimulator::update() {
    pImpl->update();
}

std::string VehicleSimulator::getTelemetry() const {
    return pImpl->getTelemetry();
}

void VehicleSimulator::setTelemetryCallback(TelemetryCallback callback) {
    pImpl->setTelemetryCallback(std::move(callback));
}

bool VehicleSimulator::hasNewData() const {
    return pImpl->hasNewData();
}

std::string VehicleSimulator::getLatestTelemetry() {
    return pImpl->getLatestTelemetry();
}

} // namespace vehicle_sim
