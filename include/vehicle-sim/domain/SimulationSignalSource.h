#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/VehicleSim.h"
#include <memory>

namespace vehicle_sim::domain {

/**
 * Adapter for VehicleSimulator to ISignalSource
 *
 * VehicleSimulator uses an update loop pattern (update() -> getLatestSignal()).
 * This adapts it to the ISignalSource interface (start/stop/latestSignal).
 */
class SimulationSignalSource : public ISignalSource {
public:
    explicit SimulationSignalSource(std::unique_ptr<VehicleSimulator> simulator) noexcept;
    ~SimulationSignalSource() override;

    SimulationSignalSource(const SimulationSignalSource&) = delete;
    SimulationSignalSource& operator=(const SimulationSignalSource&) = delete;

    [[nodiscard]] VehicleSignal latestSignal() const noexcept override;
    void start() noexcept override;
    void stop() noexcept override;

private:
    std::unique_ptr<VehicleSimulator> simulator_;
    bool running_{false};
    mutable std::mutex signalMutex_;
    VehicleSignal latestSignal_{0};
};

} // namespace vehicle_sim::domain