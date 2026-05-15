#include <gtest/gtest.h>
#include "vehicle-sim/VehicleSim.h"

using namespace vehicle_sim;

// ================================================
// VehicleSimulator Unit Tests
// TDD - Simulation logic and getLatestSignal()
// ================================================

TEST(VehicleSimulatorTest, InitializeSucceedsWithDefaultConfig)
{
    VehicleSimulator sim;
    EXPECT_TRUE(sim.initialize());
}

TEST(VehicleSimulatorTest, StartReturnsTrue)
{
    VehicleSimulator sim;
    sim.initialize();
    EXPECT_TRUE(sim.start());
}

TEST(VehicleSimulatorTest, StopDoesNotThrowWhenNotStarted)
{
    VehicleSimulator sim;
    ASSERT_NO_THROW(sim.stop());
}

TEST(VehicleSimulatorTest, UpdateWithoutStartIsNoop)
{
    VehicleSimulator sim;
    sim.update();

    // Should return default nullopt signal
    auto signal = sim.getLatestSignal();
    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
}

TEST(VehicleSimulatorTest, UpdateProducesChangingSpeed)
{
    VehicleSimulator sim;
    sim.start();

    sim.update();
    double speed1 = sim.getLatestSignal().getSpeedKmh().value_or(0.0);

    // Run many ticks to allow speed to change
    for (int i = 0; i < 50; ++i) {
        sim.update();
    }
    double speed2 = sim.getLatestSignal().getSpeedKmh().value_or(0.0);

    // Speed should have changed after 50 ticks
    EXPECT_NE(speed1, speed2);
}

TEST(VehicleSimulatorTest, UpdateProducesChangingThrottle)
{
    VehicleSimulator sim;
    sim.start();

    std::vector<double> throttles;
    for (int i = 0; i < 100; ++i) {
        sim.update();
        throttles.push_back(sim.getLatestSignal().getThrottlePercent().value_or(0.0));
    }

    // Throttle should vary (sine wave), not be constant
    auto minIt = std::min_element(throttles.begin(), throttles.end());
    auto maxIt = std::max_element(throttles.begin(), throttles.end());
    EXPECT_LT(*minIt, *maxIt);
}

TEST(VehicleSimulatorTest, GetLatestSignalReturnsValidVehicleSignal)
{
    VehicleSimulator sim;
    sim.start();
    sim.update();

    auto signal = sim.getLatestSignal();

    // All values should be present when simulator is running
    ASSERT_TRUE(signal.getThrottlePercent().has_value());
    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    ASSERT_TRUE(signal.getAccelerationG().has_value());
    ASSERT_TRUE(signal.getBrakePercent().has_value());

    // Values should be reasonable ranges
    EXPECT_GE(signal.getThrottlePercent().value(), 0.0);
    EXPECT_LE(signal.getThrottlePercent().value(), 100.0);
    EXPECT_GE(signal.getSpeedKmh().value(), 0.0);
    EXPECT_GE(signal.getAccelerationG().value(), -10.0);
    EXPECT_LE(signal.getAccelerationG().value(), 10.0);
    EXPECT_GE(signal.getBrakePercent().value(), 0.0);
    EXPECT_LE(signal.getBrakePercent().value(), 100.0);
    // Timestamp should be non-zero (current UTC ms)
    EXPECT_GT(signal.getTimestampUtcMs(), 0ULL);
}

TEST(VehicleSimulatorTest, GetTelemetryReturnsJsonWithVehicleSignalFields)
{
    VehicleSimulator sim;
    sim.start();
    sim.update();

    std::string json = sim.getTelemetry();

    // JSON should contain the 4 VehicleSignal field names
    EXPECT_NE(json.find("\"throttle\":"), std::string::npos);
    EXPECT_NE(json.find("\"speed\":"), std::string::npos);
    EXPECT_NE(json.find("\"acceleration\":"), std::string::npos);
    EXPECT_NE(json.find("\"brake\":"), std::string::npos);
    // Should NOT contain legacy fields
    EXPECT_EQ(json.find("\"rpm\":"), std::string::npos);
    EXPECT_EQ(json.find("\"torque\":"), std::string::npos);
    EXPECT_EQ(json.find("\"gear\":"), std::string::npos);
}

TEST(VehicleSimulatorTest, HasNewDataReflectsRunningState)
{
    VehicleSimulator sim;
    EXPECT_FALSE(sim.hasNewData());

    sim.start();
    EXPECT_TRUE(sim.hasNewData());

    sim.stop();
    EXPECT_FALSE(sim.hasNewData());
}
