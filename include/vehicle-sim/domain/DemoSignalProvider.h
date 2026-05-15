#pragma once

#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Demo signal provider that generates realistic vehicle telemetry
 *
 * Implements pure DI — produces VehicleSignal data through the same
 * callback interface as real data sources. The pipeline doesn't know
 * or care where data comes from.
 *
 * Generates varying values simulating real driving:
 * - Speed ramping up/down
 * - RPM changes with gear
 * - Gear shifts through P→R→N→D→S
 * - Torque changes including negative for regen
 * - Throttle/brake changes
 */
class DemoSignalProvider final {
public:
    /** Consumer callback — receives const VehicleSignal& */
    using SignalCallback = std::function<void(const VehicleSignal&)>;

    /**
     * Constructor
     * @param intervalMs Update interval in milliseconds (default: 50ms = 20Hz)
     */
    explicit DemoSignalProvider(int intervalMs = 50) noexcept;

    ~DemoSignalProvider();

    // Non-copyable, non-movable (owns a thread)
    DemoSignalProvider(const DemoSignalProvider&) = delete;
    DemoSignalProvider& operator=(const DemoSignalProvider&) = delete;
    DemoSignalProvider(DemoSignalProvider&&) = delete;
    DemoSignalProvider& operator=(DemoSignalProvider&&) = delete;

    /**
     * Start generating demo signals
     * @param callback Function to receive generated signals
     */
    void start(SignalCallback callback);

    /**
     * Stop generating demo signals
     */
    void stop();

    /**
     * Check if provider is running
     */
    [[nodiscard]] bool isRunning() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace vehicle_sim::domain
