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

private:
    void generateSignals();

    int intervalMs_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    VehicleSignal latestSignal_{0};
    mutable std::mutex signalMutex_;
    double phase_{0.0};
};

} // namespace vehicle_sim::domain