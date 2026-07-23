#pragma once

#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Abstract interface for the vehicle simulator's update-loop contract.
 *
 * SimulationSignalSource adapts this loop (initialize → start → update +
 * getLatestSignal) onto the ISignalSource interface. Depending on this
 * abstraction rather than the concrete VehicleSimulator is the DI seam that
 * lets the adapter's worker-loop contracts be unit-tested with a mock,
 * without a live simulation thread (OCP/DIP — mirrors ISocket / IClock).
 */
class IVehicleSimulator {
public:
    virtual ~IVehicleSimulator() = default;

    /// Initialize the simulator with optional configuration. Returns true on success.
    /// Declared const to match VehicleSimulator's existing contract (logical
    /// initialization that does not observably mutate post-start state).
    virtual bool initialize(const std::string& configFile = "") const = 0;

    /// Start the simulation (begin producing signal data). Returns true on success.
    virtual bool start() = 0;

    /// Stop the simulation (cease producing signal data).
    virtual void stop() = 0;

    /// Advance the simulation by one tick, animating its internal state.
    virtual void update() = 0;

    /// The latest typed signal produced by the simulation.
    [[nodiscard]] virtual VehicleSignal getLatestSignal() const = 0;
};

} // namespace vehicle_sim::domain
