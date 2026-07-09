#include "vehicle-sim/domain/DemoSignalSource.h"
#include "vehicle-sim/domain/Gear.h"
#include <array>
#include <chrono>
#include <cmath>

namespace vehicle_sim::domain {

DemoSignalSource::DemoSignalSource(int intervalMs) noexcept
    : intervalMs_(intervalMs)
{
}

DemoSignalSource::~DemoSignalSource() {
    stop();
}

VehicleSignal DemoSignalSource::latestSignal() const noexcept {
    std::scoped_lock lock(signalMutex_);
    return latestSignal_;
}

void DemoSignalSource::start() noexcept {
    if (running_.load()) return;

    worker_ = std::thread(&DemoSignalSource::generateSignals, this);
    running_.store(true);
}

void DemoSignalSource::stop() noexcept {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool DemoSignalSource::isRunning() const noexcept {
    return running_.load();
}

void DemoSignalSource::generateSignals() {
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        // Advance phase for smooth oscillation
        phase_ += 0.05;
        if (phase_ > 2.0 * M_PI) {
            phase_ -= 2.0 * M_PI;
        }

        // Simulate driving cycle with sine waves
        double cycle = (std::sin(phase_) + 1.0) / 2.0;

        double speedKmh = cycle * 100.0;
        double throttlePercent = cycle * 80.0;
        double brakePercent = (cycle > 0.7) ? (cycle - 0.7) * 333.0 : 0.0;
        if (brakePercent > 100.0) brakePercent = 100.0;
        double accelerationG = (cycle < 0.7) ? 0.3 : -0.5;
        double baseRpm = 1000.0 + (cycle * 6000.0);
        double motorTorqueNm;

        if (cycle < 0.6) {
            motorTorqueNm = 100.0 + (cycle * 300.0);
        } else {
            motorTorqueNm = -50.0 - ((cycle - 0.6) * 200.0);
        }

        double steeringAngleDeg = std::sin(phase_ * 2.0) * 30.0;
        double motorHvVoltage = 350.0 + (cycle * 50.0);
        double motorHvCurrent = std::abs(motorTorqueNm) / 10.0;

        // Gear: cycles through P, R, N, D using canonical constants
        const std::array<std::int32_t, 5> gears = {
            Gear::PARK,
            Gear::REVERSE,
            Gear::NEUTRAL,
            Gear::AUTO_1,
            Gear::AUTO_2
        };
        auto newGearIndex = static_cast<int>(cycle * 5.0);
        if (newGearIndex > 4) newGearIndex = 4;
        std::int32_t gearSelector = gears[newGearIndex];

        VehicleSignal signal(VehicleSignal::Params{
            .timestampUtcMs = static_cast<std::uint64_t>(timestamp),
            .throttlePercent = throttlePercent,
            .speedKmh = speedKmh,
            .accelerationG = accelerationG,
            .brakePercent = brakePercent,
            .steeringAngleDeg = steeringAngleDeg,
            .motorRpm = baseRpm,
            .motorHvVoltage = motorHvVoltage,
            .motorHvCurrent = motorHvCurrent,
            .motorTorqueNm = motorTorqueNm,
            .gearSelector = gearSelector
        });

        {
            std::scoped_lock lock(signalMutex_);
            latestSignal_ = signal;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
    }
}

} // namespace vehicle_sim::domain