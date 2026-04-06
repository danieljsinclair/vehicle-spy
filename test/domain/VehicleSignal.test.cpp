#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// VehicleSignal Unit Tests
// TDD - Full test coverage for value object
// ================================================

TEST(VehicleSignalTest, ConstructsWithValidValues)
{
    const VehicleSignal signal(50.0, 100.0, 0.5, 25.0, 123456789ULL);

    EXPECT_DOUBLE_EQ(signal.getThrottlePercent(), 50.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getAccelerationG(), 0.5);
    EXPECT_DOUBLE_EQ(signal.getBrakePercent(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
}

TEST(VehicleSignalTest, ClampsThrottleToValidRange)
{
    EXPECT_DOUBLE_EQ(VehicleSignal(-10.0, 0, 0, 0, 0).getThrottlePercent(), 0.0);
    EXPECT_DOUBLE_EQ(VehicleSignal(150.0, 0, 0, 0, 0).getThrottlePercent(), 100.0);
}

TEST(VehicleSignalTest, ClampsSpeedToValidRange)
{
    EXPECT_DOUBLE_EQ(VehicleSignal(0, -5.0, 0, 0, 0).getSpeedKmh(), 0.0);
    EXPECT_DOUBLE_EQ(VehicleSignal(0, 400.0, 0, 0, 0).getSpeedKmh(), 300.0);
}

TEST(VehicleSignalTest, ClampsAccelerationToValidRange)
{
    EXPECT_DOUBLE_EQ(VehicleSignal(0, 0, -10.0, 0, 0).getAccelerationG(), -5.0);
    EXPECT_DOUBLE_EQ(VehicleSignal(0, 0, 10.0, 0, 0).getAccelerationG(), 5.0);
}

TEST(VehicleSignalTest, ClampsBrakeToValidRange)
{
    EXPECT_DOUBLE_EQ(VehicleSignal(0, 0, 0, -5.0, 0).getBrakePercent(), 0.0);
    EXPECT_DOUBLE_EQ(VehicleSignal(0, 0, 0, 120.0, 0).getBrakePercent(), 100.0);
}

TEST(VehicleSignalTest, ValueEqualityWorks)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345);
    const VehicleSignal c(51.0, 100.0, 0.5, 25.0, 12345);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalTest, ValueInequalityWorks)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345);
    const VehicleSignal b(51.0, 100.0, 0.5, 25.0, 12345);

    EXPECT_NE(a, b);
}

// NOTE: Immutability test removed
// Original test used SUCCEED() placeholder which violated TDD
// Immutability is enforced by compile-time through API design:
// - No mutator methods exist in VehicleSignal class
// - All methods are const
// - Return by value only
// This is a compile-time guarantee, not a runtime test
