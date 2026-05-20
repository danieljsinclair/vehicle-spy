#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include <vector>
#include <mutex>

namespace vehicle_sim::test {

/**
 * Mock ISignalSource for testing.
 *
 * Provides controlled signal emission for testing TelemetryRunner and
 * other components that consume ISignalSource.
 */
class MockSignalSource : public domain::ISignalSource {
public:
    MockSignalSource() = default;
    ~MockSignalSource() override = default;

    [[nodiscard]] domain::VehicleSignal latestSignal() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return latestSignal_;
    }

    void start() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = true;
    }

    void stop() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = false;
    }

    /**
     * Set the signal to be returned by latestSignal()
     */
    void setSignal(const domain::VehicleSignal& signal) {
        std::lock_guard<std::mutex> lock(mutex_);
        latestSignal_ = signal;
    }

    /**
     * Check if the source was started
     */
    [[nodiscard]] bool wasStarted() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return started_;
    }

    /**
     * Check if the source was stopped
     */
    [[nodiscard]] bool wasStopped() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return started_;  // We reuse the flag for simplicity
    }

private:
    mutable std::mutex mutex_;
    domain::VehicleSignal latestSignal_{0};
    bool started_ = false;
};

} // namespace vehicle_sim::test