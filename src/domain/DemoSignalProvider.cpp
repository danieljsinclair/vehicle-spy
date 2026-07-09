#include "vehicle-sim/domain/DemoSignalProvider.h"
#include "vehicle-sim/domain/Gear.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace vehicle_sim::domain {

class DemoSignalProvider::Impl {
public:
    explicit Impl(int intervalMs, std::shared_ptr<util::IClock> clock) noexcept
        : intervalMs_(intervalMs)
        , clock_(std::move(clock))
    {
    }

    ~Impl() {
        stop();
    }

    void start(SignalCallback callback) {
        if (running_.load()) return;

        callback_ = std::move(callback);
        running_.store(true);
        worker_ = std::thread(&Impl::generateSignals, this);
    }

    void stop() {
        // Deterministic barrier: clear running_ under the lock so the tick
        // loop's waitFor predicate (!running_) is observed, then notify the cv
        // to release a parked waiter, then join. After stop() returns, no
        // further callback invocations occur.
        {
            std::scoped_lock lk(mutex_);
            running_.store(false);
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    [[nodiscard]] bool isRunning() const noexcept {
        return running_.load();
    }

private:
    void generateSignals() {
        while (running_.load()) {
            // Timestamp is derived from the injected clock so a FakeClock yields
            // strictly-increasing, deterministic timestamps in tests (real
            // steady_clock would assign identical ms to sub-ms-spaced ticks).
            auto now = clock_->now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            VehicleSignal signal = generateSignal(static_cast<std::uint64_t>(timestamp));
            callback_(signal);

            // Clock-aware wait: an injected FakeClock returns promptly when its
            // virtual time is advanced past the deadline (by the test thread),
            // making the tick loop deterministic. A SystemClock blocks for real
            // wall-clock time. The predicate releases the waiter on stop().
            std::unique_lock lock(mutex_);
            clock_->waitFor(cv_, lock,
                            [this] { return !running_.load(); },
                            clock_->now() + std::chrono::milliseconds(intervalMs_));
        }
    }

    VehicleSignal generateSignal(std::uint64_t timestampUtcMs) {
        // Advance phase for smooth oscillation
        phase_ += 0.05;
        if (phase_ > 2.0 * M_PI) {
            phase_ -= 2.0 * M_PI;
        }

        // Simulate driving cycle with sine waves
        double cycle = (std::sin(phase_) + 1.0) / 2.0; // 0.0 to 1.0

        // Speed: ramps 0-100 km/h over cycle
        double speedKmh = cycle * 100.0;

        // Throttle: follows speed but with lead
        double throttlePercent = cycle * 80.0;

        // Brake: activates when decelerating (cycle > 0.7)
        double brakePercent = (cycle > 0.7) ? (cycle - 0.7) * 333.0 : 0.0;
        if (brakePercent > 100.0) brakePercent = 100.0;

        // Acceleration: positive when accelerating, negative when braking
        double accelerationG = (cycle < 0.7) ? 0.3 : -0.5;

        // RPM: varies with speed and gear
        double baseRpm = 1000.0 + (cycle * 6000.0);
        double motorRpm = baseRpm;

        // Gear: cycles through P, R, N, D based on phase using canonical constants
        const std::array<std::int32_t, 5> gears = {
            Gear::PARK,
            Gear::REVERSE,
            Gear::NEUTRAL,
            Gear::AUTO_1,
            Gear::AUTO_2
        };
        auto newGearIndex = static_cast<int>(cycle * 5.0);
        if (newGearIndex > 4) newGearIndex = 4;
        if (newGearIndex != gearIndex_) {
            gearIndex_ = newGearIndex;
        }
        std::int32_t gearSelector = gears[gearIndex_];

        // Torque: positive for acceleration, negative for regen
        double motorTorqueNm;
        if (cycle < 0.6) {
            // Accelerating
            motorTorqueNm = 100.0 + (cycle * 300.0);
        } else {
            // Regenerative braking
            motorTorqueNm = -50.0 - ((cycle - 0.6) * 200.0);
        }

        // Steering: slight oscillation
        double steeringAngleDeg = std::sin(phase_ * 2.0) * 30.0;

        // HV voltage: varies with load
        double motorHvVoltage = 350.0 + (cycle * 50.0);

        // HV current: proportional to torque
        double motorHvCurrent = std::abs(motorTorqueNm) / 10.0;

        return VehicleSignal(VehicleSignal::Params{
            .timestampUtcMs = timestampUtcMs,
            .throttlePercent = throttlePercent,
            .speedKmh = speedKmh,
            .accelerationG = accelerationG,
            .brakePercent = brakePercent,
            .steeringAngleDeg = steeringAngleDeg,
            .motorRpm = motorRpm,
            .motorHvVoltage = motorHvVoltage,
            .motorHvCurrent = motorHvCurrent,
            .motorTorqueNm = motorTorqueNm,
            .gearSelector = gearSelector
        });
    }

    int intervalMs_;
    std::shared_ptr<util::IClock> clock_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    SignalCallback callback_;

    // Simulation state
    double phase_{0.0};
    int gearIndex_{0};
};

DemoSignalProvider::DemoSignalProvider(
    int intervalMs, std::shared_ptr<util::IClock> clock) noexcept
    : pImpl(std::make_unique<Impl>(intervalMs, std::move(clock)))
{
}

DemoSignalProvider::~DemoSignalProvider() = default;

void DemoSignalProvider::start(SignalCallback callback) {
    pImpl->start(std::move(callback));
}

void DemoSignalProvider::stop() {
    pImpl->stop();
}

bool DemoSignalProvider::isRunning() const noexcept {
    return pImpl->isRunning();
}

} // namespace vehicle_sim::domain
