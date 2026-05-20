#include "vehicle-sim/domain/SimulationSignalSource.h"

namespace vehicle_sim::domain {

SimulationSignalSource::SimulationSignalSource(std::unique_ptr<VehicleSimulator> simulator) noexcept
    : simulator_(std::move(simulator))
    , running_(false)
    , latestSignal_(0)
{
}

SimulationSignalSource::~SimulationSignalSource() {
    stop();
}

VehicleSignal SimulationSignalSource::latestSignal() const noexcept {
    std::lock_guard<std::mutex> lock(signalMutex_);
    return latestSignal_;
}

void SimulationSignalSource::start() noexcept {
    if (running_) return;

    simulator_->initialize();
    simulator_->start();
    running_ = true;
}

void SimulationSignalSource::stop() noexcept {
    if (!running_) return;

    simulator_->stop();
    running_ = false;
}

} // namespace vehicle_sim::domain