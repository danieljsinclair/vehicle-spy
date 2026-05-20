#include <gtest/gtest.h>
#include "vehicle-sim/domain/TelemetrySignal.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

// ================================================
// TelemetrySignal Unit Tests
// TDD - Full test coverage for output value object
// ================================================

TEST(TelemetrySignalTest, ConstructsWithValidValues)
{
    // rpm, gear, torqueNm, speedKmh, throttlePercent, timestamp
    const TelemetrySignal signal(3000.0, 3, 450.0, 100.0, 50.0, 123456789ULL);

    EXPECT_DOUBLE_EQ(signal.getRpm(), 3000.0);
    EXPECT_EQ(signal.getGear(), 3);
    EXPECT_DOUBLE_EQ(signal.getTorqueNm(), 450.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getThrottlePercent(), 50.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
}

TEST(TelemetrySignalTest, ClampsRpmToValidRange)
{
    EXPECT_DOUBLE_EQ(TelemetrySignal(-100.0, 1, 0, 0, 0, 1000).getRpm(), 0.0);
    EXPECT_DOUBLE_EQ(TelemetrySignal(15000.0, 1, 0, 0, 0, 1000).getRpm(), 12000.0);
}

TEST(TelemetrySignalTest, ClampsGearToValidRange)
{
    EXPECT_EQ(TelemetrySignal(0, -3, 0, 0, 0, 1000).getGear(), Gear::REVERSE);
    EXPECT_EQ(TelemetrySignal(0, 10, 0, 0, 0, 1000).getGear(), Gear::GEAR_6);
}

TEST(TelemetrySignalTest, ClampsTorqueToValidRange)
{
    EXPECT_DOUBLE_EQ(TelemetrySignal(0, 1, -100.0, 0, 0, 1000).getTorqueNm(), 0.0);
    EXPECT_DOUBLE_EQ(TelemetrySignal(0, 1, 2000.0, 0, 0, 1000).getTorqueNm(), 1500.0);
}

TEST(TelemetrySignalTest, ClampsThrottleToValidRange)
{
    EXPECT_DOUBLE_EQ(TelemetrySignal(0, 1, 0, 0, -10.0, 1000).getThrottlePercent(), 0.0);
    EXPECT_DOUBLE_EQ(TelemetrySignal(0, 1, 0, 0, 120.0, 1000).getThrottlePercent(), 100.0);
}

TEST(TelemetrySignalTest, ValueEqualityWorks)
{
    const TelemetrySignal a(3000.0, 3, 450.0, 100.0, 50.0, 12345);
    const TelemetrySignal b(3000.0, 3, 450.0, 100.0, 50.0, 12345);
    const TelemetrySignal c(3001.0, 3, 450.0, 100.0, 50.0, 12345);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(TelemetrySignalTest, ValueInequalityWorks)
{
    const TelemetrySignal a(3000.0, 3, 450.0, 100.0, 50.0, 12345);
    const TelemetrySignal b(3001.0, 3, 450.0, 100.0, 50.0, 12345);

    EXPECT_NE(a, b);
}

// NOTE: Immutability is a compile-time guarantee, not runtime behavior
// Immutability is enforced by compile-time through API design:
// - No mutator methods exist in TelemetrySignal class
// - All methods are const
// - Return by value only
// This is a compile-time guarantee, not a runtime test
