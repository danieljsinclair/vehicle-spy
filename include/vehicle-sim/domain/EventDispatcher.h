#pragma once

#include <functional>
#include <memory>

#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Thread-safe event dispatcher for VehicleSignal telemetry
 *
 * Central hub for routing telemetry events from CAN parser to:
 * - UI display layer
 * - Logging subsystem
 * - Testing consumers
 *
 * Design ensures:
 * - Thread-safe event delivery
 * - Multiple consumer support (one-to-many)
 * - 10Hz minimum update rate support
 * - Integration with TeslaSignalParser callback pattern
 * - Clean shutdown semantics
 */
class EventDispatcher final {
public:
    /** Consumer callback — receives const VehicleSignal& */
    using SignalCallback = std::function<void(const VehicleSignal&)>;

    /**
     * Constructor - initializes dispatcher with no consumers
     */
    EventDispatcher();

    /**
     * Destructor - ensures clean shutdown
     */
    ~EventDispatcher();

    /**
     * Register a consumer to receive telemetry events
     *
     * @param callback Function to call when events are available
     * @return Subscription token (unsigned int) for unregistering
     */
    unsigned int registerConsumer(SignalCallback callback);

    /**
     * Unregister a consumer by token
     *
     * @param token Subscription token from registerConsumer()
     */
    void unregisterConsumer(unsigned int token);

    /**
     * Dispatch a telemetry event to all registered consumers
     *
     * @param signal The vehicle signal to dispatch
     */
    void dispatch(const VehicleSignal& signal);

    /**
     * Clear all registered consumers
     */
    void clear();

private:
    // Implementation details to be filled in GREEN phase
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace vehicle_sim::domain
