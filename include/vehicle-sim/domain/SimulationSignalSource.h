#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/IVehicleSimulator.h"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

namespace vehicle_sim::domain {

/**
 * Adapter for an IVehicleSimulator to the ISignalSource interface.
 *
 * VehicleSimulator uses an update-loop pattern (initialize → start → update()
 * → getLatestSignal()). This adapter bridges it to the ISignalSource
 * start/stop/latestSignal contract: start() spawns a worker thread that
 * periodically drives update() and snapshots getLatestSignal() into the
 * latestSignal_ member under a mutex; latestSignal() returns that snapshot.
 *
 * Depends on IVehicleSimulator (not the concrete VehicleSimulator) so the
 * worker-loop contracts can be unit-tested with a mock simulator.
 */
class SimulationSignalSource : public ISignalSource {
public:
    /**
     * Construct the adapter over a simulator.
     *
     * @param simulator    The simulator to adapt (owned).
     * @param intervalMs   Milliseconds between worker poll ticks (default 50ms).
     */
    explicit SimulationSignalSource(std::unique_ptr<IVehicleSimulator> simulator,
                                    int intervalMs = 50) noexcept;
    ~SimulationSignalSource() override;

    SimulationSignalSource(const SimulationSignalSource&) = delete;
    SimulationSignalSource& operator=(const SimulationSignalSource&) = delete;

    [[nodiscard]] VehicleSignal latestSignal() const noexcept override;
    void start() noexcept override;
    void stop() noexcept override;

private:
    void pollSimulator();

    std::unique_ptr<IVehicleSimulator> simulator_;
    const int intervalMs_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex signalMutex_;
    VehicleSignal latestSignal_{VehicleSignal::Params{.timestampUtcMs = 0}};
};

} // namespace vehicle_sim::domain
