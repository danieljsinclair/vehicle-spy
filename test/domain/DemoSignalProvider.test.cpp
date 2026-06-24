#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <vector>
#include "vehicle-sim/domain/DemoSignalProvider.h"
#include "vehicle-sim/util/IClock.h"

using namespace vehicle_sim::domain;
using vehicle_sim::util::FakeClock;

// All DemoSignalProvider tests inject a FakeClock so the provider's tick loop is
// driven by virtual-time advance() calls instead of real wall-clock sleeps. Each
// advance(intervalMs) produces exactly one deterministic tick; stop() is the
// synchronization barrier that guarantees no callback fires after it returns.

class DemoSignalProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        signalCount = 0;
        intervalMs = 10;
        // Seed virtual time at a non-zero base so signal timestamps (derived
        // from clock_->now()) are naturally > 0, matching the contract several
        // tests assert.
        clock = std::make_shared<FakeClock>(std::chrono::steady_clock::time_point{} +
                                            std::chrono::hours(1));
    }

    void TearDown() override {
        // Ensure provider is stopped
        if (provider) {
            provider->stop();
        }
    }

    // Helper: make a provider wired to the shared FakeClock.
    void makeProvider(int ms = 10) {
        intervalMs = ms;
        provider = std::make_unique<DemoSignalProvider>(intervalMs, clock);
    }

    // Wrap a per-test callback with tick accounting so advanceTicks() can
    // synchronize with the worker: it counts callbacks and notifies tickCv_.
    // The wrapped callback runs the user's logic under tickMtx_ so the captured
    // state is visible to advanceTicks() without a separate lock.
    template <class F>
    DemoSignalProvider::SignalCallback countTicks(F userCallback) {
        return [this, userCallback](const VehicleSignal& signal) {
            {
                std::lock_guard<std::mutex> lk(tickMtx_);
                userCallback(signal);
            }
            tickCount_.fetch_add(1, std::memory_order_release);
            tickCv_.notify_one();
        };
    }

    // Drive the tick loop deterministically: each advance(intervalMs) produces
    // exactly one tick. We block until the worker reports that tick via tickCv_
    // before advancing again, so #advances == #callbacks — no coalescing, no
    // reliance on wall-clock sleeps. The 2s wait_for is only a safety net; a
    // correct FakeClock-driven loop never hits it.
    void advanceTicks(int n) {
        for (int i = 0; i < n; ++i) {
            const int before = tickCount_.load(std::memory_order_acquire);
            clock->advance(std::chrono::milliseconds(intervalMs));
            std::unique_lock<std::mutex> lk(tickMtx_);
            tickCv_.wait_for(lk, std::chrono::milliseconds(2000),
                             [&] { return tickCount_.load(std::memory_order_acquire) > before; });
        }
    }

    std::unique_ptr<DemoSignalProvider> provider;
    std::shared_ptr<FakeClock> clock;
    int intervalMs;
    std::atomic<int> signalCount;
    std::optional<VehicleSignal> lastSignal;

    // Tick synchronization between the provider callback and advanceTicks().
    std::mutex tickMtx_;
    std::condition_variable tickCv_;
    std::atomic<int> tickCount_{0};
};

// ================================================
// Lifecycle Tests
// ================================================

TEST_F(DemoSignalProviderTest, IsRunningReturnsFalseWhenNotStarted) {
    makeProvider(50);
    EXPECT_FALSE(provider->isRunning());
}

TEST_F(DemoSignalProviderTest, IsRunningReturnsTrueAfterStart) {
    makeProvider(50);

    provider->start([this](const VehicleSignal& signal) {
        signalCount++;
        lastSignal = signal;
    });

    EXPECT_TRUE(provider->isRunning());
    provider->stop();
}

TEST_F(DemoSignalProviderTest, IsRunningReturnsFalseAfterStop) {
    makeProvider(50);

    provider->start([this](const VehicleSignal& signal) {
        signalCount++;
    });

    provider->stop();

    EXPECT_FALSE(provider->isRunning());
}

// ================================================
// Signal Generation Tests
// ================================================

TEST_F(DemoSignalProviderTest, GeneratesNonZeroSignalsAfterStart) {
    makeProvider(10);

    provider->start(countTicks([this](const VehicleSignal& signal) {
        signalCount++;
        lastSignal = signal;
    }));

    advanceTicks(3);

    provider->stop();

    EXPECT_GE(signalCount.load(), 1);
    ASSERT_TRUE(lastSignal.has_value());
    EXPECT_GT(lastSignal->getThrottlePercent().value(), 0.0);
}

TEST_F(DemoSignalProviderTest, StopsGeneratingAfterStop) {
    makeProvider(10);

    provider->start(countTicks([this](const VehicleSignal& signal) {
        signalCount++;
    }));

    advanceTicks(3);

    provider->stop();

    auto countAfterStop = signalCount.load();

    // With the loop stopped and joined, advancing virtual time produces no
    // further ticks — deterministically, no wall-clock wait required.
    clock->advance(std::chrono::milliseconds(50));
    std::this_thread::yield();

    EXPECT_EQ(signalCount.load(), countAfterStop);
}

TEST_F(DemoSignalProviderTest, ProducesSignalsWithinValidVehicleSignalRanges) {
    makeProvider(10);

    provider->start(countTicks([this](const VehicleSignal& signal) {
        signalCount++;
        lastSignal = signal;
    }));

    advanceTicks(3);

    provider->stop();

    ASSERT_TRUE(lastSignal.has_value());

    EXPECT_GE(lastSignal->getThrottlePercent().value(), 0.0);
    EXPECT_LE(lastSignal->getThrottlePercent().value(), 100.0);

    EXPECT_GE(lastSignal->getSpeedKmh().value(), 0.0);

    EXPECT_GE(lastSignal->getBrakePercent().value(), 0.0);
    EXPECT_LE(lastSignal->getBrakePercent().value(), 100.0);

    EXPECT_GE(lastSignal->getMotorRpm().value(), 0.0);
    EXPECT_LE(lastSignal->getMotorRpm().value(), 20000.0);

    EXPECT_GE(lastSignal->getMotorTorqueNm().value(), -7500.0);
    EXPECT_LE(lastSignal->getMotorTorqueNm().value(), 7500.0);

    EXPECT_GE(lastSignal->getMotorHvVoltage().value(), 0.0);
    EXPECT_LE(lastSignal->getMotorHvVoltage().value(), 1000.0);

    EXPECT_GE(lastSignal->getMotorHvCurrent().value(), 0.0);
    EXPECT_LE(lastSignal->getMotorHvCurrent().value(), 50.0);
}

// ================================================
// Callback Tests
// ================================================

TEST_F(DemoSignalProviderTest, CallsRegisteredCallback) {
    makeProvider(10);

    std::atomic<bool> callbackInvoked{false};

    provider->start(countTicks([&callbackInvoked](const VehicleSignal& signal) {
        callbackInvoked = true;
    }));

    advanceTicks(3);

    provider->stop();

    EXPECT_TRUE(callbackInvoked.load());
}

TEST_F(DemoSignalProviderTest, CallbackReceivesConstReference) {
    makeProvider(10);

    std::optional<VehicleSignal> receivedSignal;

    provider->start(countTicks([&receivedSignal](const VehicleSignal& signal) {
        receivedSignal = signal;
    }));

    advanceTicks(3);

    provider->stop();

    ASSERT_TRUE(receivedSignal.has_value());
    EXPECT_GT(receivedSignal->getTimestampUtcMs(), 0ULL);
}

// ================================================
// Signal Variation Tests
// ================================================

TEST_F(DemoSignalProviderTest, SignalsVaryOverTimeNotIdentical) {
    makeProvider(1);

    std::vector<VehicleSignal> signals;

    provider->start(countTicks([&](const VehicleSignal& signal) {
        signals.push_back(signal);
    }));

    advanceTicks(5);

    provider->stop();

    EXPECT_GE(signals.size(), 3);

    bool allIdentical = true;
    for (size_t i = 1; i < signals.size(); ++i) {
        if (signals[i].getSpeedKmh().value() != signals[0].getSpeedKmh().value() ||
            signals[i].getThrottlePercent().value() != signals[0].getThrottlePercent().value() ||
            signals[i].getMotorRpm().value() != signals[0].getMotorRpm().value()) {
            allIdentical = false;
            break;
        }
    }

    EXPECT_FALSE(allIdentical);
}

TEST_F(DemoSignalProviderTest, TimestampsIncreaseMonotonically) {
    makeProvider(1);

    std::vector<std::uint64_t> timestamps;

    provider->start(countTicks([&](const VehicleSignal& signal) {
        timestamps.push_back(signal.getTimestampUtcMs());
    }));

    advanceTicks(5);

    provider->stop();

    EXPECT_GE(timestamps.size(), 3);

    for (size_t i = 1; i < timestamps.size(); ++i) {
        EXPECT_GT(timestamps[i], timestamps[i - 1]);
    }
}

// ================================================
// Gear Selector Tests
// ================================================

TEST_F(DemoSignalProviderTest, GeneratesValidGearSelectorValues) {
    makeProvider(1);

    std::set<std::int32_t> gearsSeen;

    provider->start(countTicks([&](const VehicleSignal& signal) {
        gearsSeen.insert(signal.getGearSelector().value());
    }));

    // Advance enough ticks for the simulation phase to sweep through several
    // gears (phase advances 0.05 per tick → ~125 ticks per 2π cycle, 5 gear
    // bands). ~60 ticks comfortably exercises multiple gear bands.
    advanceTicks(60);

    provider->stop();

    EXPECT_TRUE(gearsSeen.count(-2) || gearsSeen.count(-1) ||
                gearsSeen.count(0) || gearsSeen.count(4097) ||
                gearsSeen.count(4098));
}

// ================================================
// Torque Sign Tests
// ================================================

TEST_F(DemoSignalProviderTest, GeneratesPositiveTorqueForAcceleration) {
    makeProvider(1);

    std::atomic<bool> hasPositiveTorque{false};

    provider->start(countTicks([&](const VehicleSignal& signal) {
        if (signal.getMotorTorqueNm().value() > 0) {
            hasPositiveTorque.store(true);
        }
    }));

    // The first half-cycle (cycle < 0.6) produces positive torque. A handful of
    // ticks at the start of the phase sweep guarantees we observe it.
    advanceTicks(20);

    provider->stop();

    EXPECT_TRUE(hasPositiveTorque.load());
}

TEST_F(DemoSignalProviderTest, GeneratesNegativeTorqueForRegen) {
    makeProvider(1);

    std::atomic<bool> hasNegativeTorque{false};

    provider->start(countTicks([&](const VehicleSignal& signal) {
        if (signal.getMotorTorqueNm().value() < 0) {
            hasNegativeTorque.store(true);
        }
    }));

    // Regen (negative torque) occurs when cycle >= 0.6. ~125 ticks span a full
    // 2π phase cycle, so ~80 ticks guarantees we reach the regen band.
    advanceTicks(80);

    provider->stop();

    EXPECT_TRUE(hasNegativeTorque.load());
}
