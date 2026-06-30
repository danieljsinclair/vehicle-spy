#include "vehicle-sim/VehicleSim.h"
#include <iostream>
#include <cmath>
#include <sstream>
#include <chrono>

namespace vehicle_sim {

static std::uint64_t nowUtcMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// Define the PIMPL implementation
class VehicleSimulator::Impl {
public:
    Impl() = default;

    ~Impl() = default;

    bool initialize(const std::string& config_file) const {
#ifndef VEHICLE_SIM_TEST_SILENTLY
        std::cout << "[VehicleSimulator] Initializing with config: "
                  << (config_file.empty() ? "default" : config_file) << std::endl;
#endif
        return true;
    }

    bool start() {
        running_ = true;
#ifndef VEHICLE_SIM_TEST_SILENTLY
        std::cout << "[VehicleSimulator] Started" << std::endl;
#endif
        return true;
    }

    void stop() {
        running_ = false;
#ifndef VEHICLE_SIM_TEST_SILENTLY
        std::cout << "[VehicleSimulator] Stopped" << std::endl;
#endif
    }

    void update() {
        if (!running_) return;

        tick_++;
        double t = tick_ * 0.1;

        // Throttle: oscillates between 0-60% with sine wave
        double throttle = 30.0 + 25.0 * std::sin(t * 0.3) + 5.0 * std::sin(t * 1.7);

        // Speed: follows throttle with lag (vehicle accelerates/decelerates)
        double targetSpeed = throttle * 1.8;
        speed_ += (targetSpeed - speed_) * 0.05;

        // Acceleration: derivative of speed
        double acceleration = (speed_ - lastSpeed_) * 2.0;
        lastSpeed_ = speed_;

        // Brake: occasionally pulses
        double brake = (std::fmod(t, 8.0) > 7.0)
            ? 20.0 + 10.0 * std::sin(t * 5.0)
            : 0.0;

        // Store latest signal
        latestSignal_ = domain::VehicleSignal(
            nowUtcMs(),
            throttle,
            speed_,
            acceleration,
            brake
        );

        // Fire callback if set
        if (callback_) {
            callback_(getTelemetry());
        }
    }

    domain::VehicleSignal getLatestSignal() const {
        return latestSignal_;
    }

    std::string getTelemetry() const {
        std::ostringstream json;
        json << "{"
             << "\"throttle\":" << latestSignal_.getThrottlePercent().value_or(0.0) << ","
             << "\"speed\":" << latestSignal_.getSpeedKmh().value_or(0.0) << ","
             << "\"acceleration\":" << latestSignal_.getAccelerationG().value_or(0.0) << ","
             << "\"brake\":" << latestSignal_.getBrakePercent().value_or(0.0)
             << "}";
        return json.str();
    }

    void setTelemetryCallback(TelemetryCallback callback) {
        callback_ = std::move(callback);
    }

    bool hasNewData() const {
        return running_;
    }

    std::string getLatestTelemetry() const {
        return getTelemetry();
    }

private:
    bool running_{false};
    int tick_{0};
    double speed_{0.0};
    double lastSpeed_{0.0};
    domain::VehicleSignal latestSignal_{0, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
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

domain::VehicleSignal VehicleSimulator::getLatestSignal() const {
    return pImpl->getLatestSignal();
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
