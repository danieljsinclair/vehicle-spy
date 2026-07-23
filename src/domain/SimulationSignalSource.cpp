#include "vehicle-sim/domain/SimulationSignalSource.h"

#include <chrono>

namespace vehicle_sim::domain {

SimulationSignalSource::SimulationSignalSource(
    std::unique_ptr<IVehicleSimulator> simulator,
    int intervalMs) noexcept
    : simulator_(std::move(simulator)),
      intervalMs_(intervalMs)
{
}

SimulationSignalSource::~SimulationSignalSource() {
    SimulationSignalSource::stop();
}

VehicleSignal SimulationSignalSource::latestSignal() const noexcept {
    std::scoped_lock lock(signalMutex_);
    return latestSignal_;
}

void SimulationSignalSource::start() noexcept {
    if (bool expected = false; !running_.compare_exchange_strong(expected, true)) {
        return;  // Already running — second start is a no-op (idempotent).
    }

    simulator_->initialize();
    simulator_->start();

    worker_ = std::thread(&SimulationSignalSource::pollSimulator, this);
}

void SimulationSignalSource::stop() noexcept {
    if (!running_.exchange(false)) {
        return;  // Was not running — nothing to stop (idempotent).
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    simulator_->stop();
}

void SimulationSignalSource::pollSimulator() {
    while (running_.load()) {
        simulator_->update();
        VehicleSignal signal = simulator_->getLatestSignal();

        {
            std::scoped_lock lock(signalMutex_);
            latestSignal_ = signal;
        }

        if (!running_.load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
    }
}

} // namespace vehicle_sim::domain
