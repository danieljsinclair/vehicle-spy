#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <optional>
#include <atomic>
#include "vehicle-sim/domain/DemoSignalProvider.h"

using namespace vehicle_sim::domain;

class DemoSignalProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        signalCount = 0;
    }

    void TearDown() override {
        // Ensure provider is stopped
        if (provider) {
            provider->stop();
        }
    }

    std::unique_ptr<DemoSignalProvider> provider;
    std::atomic<int> signalCount;
    std::optional<VehicleSignal> lastSignal;
};

// ================================================
// Lifecycle Tests
// ================================================

TEST_F(DemoSignalProviderTest, IsRunningReturnsFalseWhenNotStarted) {
    provider = std::make_unique<DemoSignalProvider>(50);
    EXPECT_FALSE(provider->isRunning());
}

TEST_F(DemoSignalProviderTest, IsRunningReturnsTrueAfterStart) {
    provider = std::make_unique<DemoSignalProvider>(50);

    provider->start([this](const VehicleSignal& signal) {
        signalCount++;
        lastSignal = signal;
    });

    EXPECT_TRUE(provider->isRunning());
    provider->stop();
}

TEST_F(DemoSignalProviderTest, IsRunningReturnsFalseAfterStop) {
    provider = std::make_unique<DemoSignalProvider>(50);

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
    provider = std::make_unique<DemoSignalProvider>(10);

    provider->start([this](const VehicleSignal& signal) {
        signalCount++;
        lastSignal = signal;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    provider->stop();

    EXPECT_GE(signalCount, 1);
    ASSERT_TRUE(lastSignal.has_value());
    EXPECT_GT(lastSignal->getThrottlePercent().value(), 0.0);
}

TEST_F(DemoSignalProviderTest, StopsGeneratingAfterStop) {
    provider = std::make_unique<DemoSignalProvider>(10);

    provider->start([this](const VehicleSignal& signal) {
        signalCount++;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    provider->stop();

    auto countAfterStop = signalCount.load();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(signalCount.load(), countAfterStop);
}

TEST_F(DemoSignalProviderTest, ProducesSignalsWithinValidVehicleSignalRanges) {
    provider = std::make_unique<DemoSignalProvider>(10);

    provider->start([this](const VehicleSignal& signal) {
        signalCount++;
        lastSignal = signal;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

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
    provider = std::make_unique<DemoSignalProvider>(10);

    std::atomic<bool> callbackInvoked{false};

    provider->start([&callbackInvoked](const VehicleSignal& signal) {
        callbackInvoked = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    provider->stop();

    EXPECT_TRUE(callbackInvoked);
}

TEST_F(DemoSignalProviderTest, CallbackReceivesConstReference) {
    provider = std::make_unique<DemoSignalProvider>(10);

    std::optional<VehicleSignal> receivedSignal;

    provider->start([&receivedSignal](const VehicleSignal& signal) {
        receivedSignal = signal;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    provider->stop();

    ASSERT_TRUE(receivedSignal.has_value());
    EXPECT_GT(receivedSignal->getTimestampUtcMs(), 0ULL);
}

// ================================================
// Signal Variation Tests
// ================================================

TEST_F(DemoSignalProviderTest, SignalsVaryOverTimeNotIdentical) {
    provider = std::make_unique<DemoSignalProvider>(10);

    std::vector<VehicleSignal> signals;

    provider->start([&signals](const VehicleSignal& signal) {
        signals.push_back(signal);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
    provider = std::make_unique<DemoSignalProvider>(10);

    std::vector<std::uint64_t> timestamps;

    provider->start([&timestamps](const VehicleSignal& signal) {
        timestamps.push_back(signal.getTimestampUtcMs());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
    provider = std::make_unique<DemoSignalProvider>(5);

    std::set<std::int32_t> gearsSeen;

    provider->start([&gearsSeen](const VehicleSignal& signal) {
        gearsSeen.insert(signal.getGearSelector().value());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    provider->stop();

    EXPECT_TRUE(gearsSeen.count(-2) || gearsSeen.count(-1) ||
                gearsSeen.count(0) || gearsSeen.count(4097) ||
                gearsSeen.count(4098));
}

// ================================================
// Torque Sign Tests
// ================================================

TEST_F(DemoSignalProviderTest, GeneratesPositiveTorqueForAcceleration) {
    provider = std::make_unique<DemoSignalProvider>(10);

    std::atomic<bool> hasPositiveTorque{false};

    provider->start([&hasPositiveTorque](const VehicleSignal& signal) {
        if (signal.getMotorTorqueNm().value() > 0) {
            hasPositiveTorque = true;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    provider->stop();

    EXPECT_TRUE(hasPositiveTorque);
}

TEST_F(DemoSignalProviderTest, GeneratesNegativeTorqueForRegen) {
    provider = std::make_unique<DemoSignalProvider>(10);

    std::atomic<bool> hasNegativeTorque{false};

    provider->start([&hasNegativeTorque](const VehicleSignal& signal) {
        if (signal.getMotorTorqueNm().value() < 0) {
            hasNegativeTorque = true;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    provider->stop();

    EXPECT_TRUE(hasNegativeTorque);
}
