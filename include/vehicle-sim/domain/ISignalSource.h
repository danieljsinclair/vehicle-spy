#pragma once

#include <cstdint>
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Abstract interface for signal data sources.
 *
 * Defines the contract for any class that provides vehicle telemetry data.
 * All concrete sources (demo, BLE, file replay, etc.) must implement this.
 *
 * Thread-safety: Implementations must handle thread safety internally.
 */
class ISignalSource {
public:
    virtual ~ISignalSource() = default;

    /**
     * Get the latest signal snapshot from this source.
     *
     * @return Current VehicleSignal state
     */
    [[nodiscard]] virtual VehicleSignal latestSignal() const noexcept = 0;

    /**
     * Start the signal source (begin emitting signals).
     */
    virtual void start() = 0;

    /**
     * Stop the signal source (cease emitting signals).
     */
    virtual void stop() = 0;
};

} // namespace vehicle_sim::domain