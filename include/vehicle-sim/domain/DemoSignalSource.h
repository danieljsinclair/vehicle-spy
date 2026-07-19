#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>

namespace vehicle_sim::domain {

class DemoSignalSource final : public ISignalSource {
public:
    using SignalCallback = std::function<void(const VehicleSignal&)>;

    /**
     * Construct a demo signal source.
     *
     * @param intervalMs Milliseconds between signal updates (default: 50ms = 20Hz)
     */
    explicit DemoSignalSource(int intervalMs = 50) noexcept;

    ~DemoSignalSource() override;

    // Non-copyable, movable
    DemoSignalSource(const DemoSignalSource&) = delete;
    DemoSignalSource& operator=(const DemoSignalSource&) = delete;

    [[nodiscard]] VehicleSignal latestSignal() const noexcept override;
    void start() noexcept override;
    void stop() noexcept override;

    [[nodiscard]] bool isRunning() const noexcept;

    /**
     * Pure per-step signal computation (SRP seam: math separated from the
     * threaded timing loop in generateSignals()). Given a phase angle and a
     * timestamp, returns the fully-formed VehicleSignal for that step. Has no
     * side effects and touches no clock/thread, so it is directly unit-testable.
     *
     * @param phase           oscillation phase (radians); periodicity 2π.
     * @param timestampUtcMs  timestamp stamped onto the produced signal.
     * @return computed VehicleSignal.
     */
    [[nodiscard]] static VehicleSignal computeNextSignal(
        double phase,
        std::uint64_t timestampUtcMs) noexcept;

private:
    void generateSignals();

    int intervalMs_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    VehicleSignal latestSignal_{VehicleSignal::Params{.timestampUtcMs = 0}};
    mutable std::mutex signalMutex_;
    double phase_{0.0};
};

} // namespace vehicle_sim::domain