#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// VehicleSignal EV Fields Extension Tests
// TDD - Tests for EV telemetry extension based on verified DBC data
//
// EV signal definitions from verified DBC sources:
//   - joshwardell/model3dbc Model3CAN.dbc (Tesla Model 3/Y)
//   - commaai/opendbc vw_mqbevo.dbc (Audi e-tron 2021)
//
// Motor/Drivetrain (Tesla CMPD_state, CAN 680):
//   - Motor RPM: 0-20000, scale 10, unit RPM
//   - Motor/HV voltage: 0-1000V, scale 0.5, unit V
//   - Motor/HV current: 0-50A, scale 0.1, unit A
//   - Motor power: 0-20000W, scale 10, unit W
//
// Regen Power (Audi BEM_01, CAN 777):
//   - Regen power: 0-12700W, scale 50, unit W
// ================================================

// ================================================
// Backward Compatibility Tests
// ================================================

TEST(VehicleSignalEVTest, ConstructsWithDefaultsForEVFields)
{
    const VehicleSignal signal(50.0, 100.0, 0.5, 25.0, 123456789ULL);

    // Base VehicleSignal fields should work
    EXPECT_DOUBLE_EQ(signal.getThrottlePercent(), 50.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getAccelerationG(), 0.5);
    EXPECT_DOUBLE_EQ(signal.getBrakePercent(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);

    // EV fields should default to zero for backward compatibility
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorPower(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getRegenPower(), 0.0);
}

TEST(VehicleSignalEVTest, ConstructsWithExplicitEVFields)
{
    const VehicleSignal signal(
        50.0,          // throttlePercent
        100.0,         // speedKmh
        0.5,           // accelerationG
        25.0,          // brakePercent
        123456789ULL,  // timestampUtcMs
        45.0,          // steeringAngleDeg
        5000.0,        // motorRpm
        400.0,         // motorHvVoltage
        25.0,          // motorHvCurrent
        10000.0,       // motorPower
        5000.0         // regenPower
    );

    // Base VehicleSignal fields
    EXPECT_DOUBLE_EQ(signal.getThrottlePercent(), 50.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getAccelerationG(), 0.5);
    EXPECT_DOUBLE_EQ(signal.getBrakePercent(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
    EXPECT_DOUBLE_EQ(signal.getSteeringAngleDeg(), 45.0);

    // EV fields
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 5000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 400.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 25.0);
    EXPECT_DOUBLE_EQ(signal.getMotorPower(), 10000.0);
    EXPECT_DOUBLE_EQ(signal.getRegenPower(), 5000.0);
}

// ================================================
// Motor RPM Tests (0-20000 from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorRpm)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 5000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 5000.0);
}

TEST(VehicleSignalEVTest, ClampsMotorRpmToVerifiedRange)
{
    // Test lower clamp (0)
    const VehicleSignal low(0, 0, 0, 0, 0, 0, -1000.0);
    EXPECT_DOUBLE_EQ(low.getMotorRpm(), 0.0);

    // Test upper clamp (20000)
    const VehicleSignal high(0, 0, 0, 0, 0, 0, 25000.0);
    EXPECT_DOUBLE_EQ(high.getMotorRpm(), 20000.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorRpmAtVerifiedMaximum)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 20000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 20000.0);
}

// ================================================
// Motor/HV Voltage Tests (0-1000V from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorHvVoltage)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 400.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 400.0);
}

TEST(VehicleSignalEVTest, ClampsMotorHvVoltageToVerifiedRange)
{
    // Test lower clamp (0V)
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, -100.0);
    EXPECT_DOUBLE_EQ(low.getMotorHvVoltage(), 0.0);

    // Test upper clamp (1000V)
    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 1200.0);
    EXPECT_DOUBLE_EQ(high.getMotorHvVoltage(), 1000.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorHvVoltageAtVerifiedMaximum)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 1000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 1000.0);
}

// ================================================
// Motor/HV Current Tests (0-50A from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorHvCurrent)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 25.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 25.0);
}

TEST(VehicleSignalEVTest, ClampsMotorHvCurrentToVerifiedRange)
{
    // Test lower clamp (0A)
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, 0, -10.0);
    EXPECT_DOUBLE_EQ(low.getMotorHvCurrent(), 0.0);

    // Test upper clamp (50A)
    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 0, 75.0);
    EXPECT_DOUBLE_EQ(high.getMotorHvCurrent(), 50.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorHvCurrentAtVerifiedMaximum)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 50.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 50.0);
}

// ================================================
// Motor Power Tests (0-20000W from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorPower)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, 10000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorPower(), 10000.0);
}

TEST(VehicleSignalEVTest, ClampsMotorPowerToVerifiedRange)
{
    // Test lower clamp (0W)
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, 0, 0, -5000.0);
    EXPECT_DOUBLE_EQ(low.getMotorPower(), 0.0);

    // Test upper clamp (20000W)
    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 0, 0, 25000.0);
    EXPECT_DOUBLE_EQ(high.getMotorPower(), 20000.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorPowerAtVerifiedMaximum)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, 20000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorPower(), 20000.0);
}

// ================================================
// Regen Power Tests (0-12700W from Audi BEM_01)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesRegenPower)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5000.0);
    EXPECT_DOUBLE_EQ(signal.getRegenPower(), 5000.0);
}

TEST(VehicleSignalEVTest, ClampsRegenPowerToVerifiedRange)
{
    // Test lower clamp (0W)
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000.0);
    EXPECT_DOUBLE_EQ(low.getRegenPower(), 0.0);

    // Test upper clamp (12700W)
    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15000.0);
    EXPECT_DOUBLE_EQ(high.getRegenPower(), 12700.0);
}

TEST(VehicleSignalEVTest, AcceptsRegenPowerAtVerifiedMaximum)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12700.0);
    EXPECT_DOUBLE_EQ(signal.getRegenPower(), 12700.0);
}

// ================================================
// Equality/Inequality Tests (includes EV fields)
// ================================================

TEST(VehicleSignalEVTest, EqualityIncludesAllEVFields)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 45.0, 5000.0, 400.0, 25.0, 10000.0, 5000.0);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 45.0, 5000.0, 400.0, 25.0, 10000.0, 5000.0);
    const VehicleSignal c(50.0, 100.0, 0.5, 25.0, 12345, 45.0, 5000.0, 400.0, 25.0, 10000.0, 5001.0);  // Different regen

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalEVTest, InequalityDetectsDifferenceInMotorRpm)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 0, 5000.0);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 0, 5001.0);

    EXPECT_NE(a, b);
}

TEST(VehicleSignalEVTest, InequalityDetectsDifferenceInMotorHvVoltage)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 400.0);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 401.0);

    EXPECT_NE(a, b);
}

TEST(VehicleSignalEVTest, InequalityDetectsDifferenceInMotorHvCurrent)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 0, 25.0);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 0, 26.0);

    EXPECT_NE(a, b);
}

TEST(VehicleSignalEVTest, InequalityDetectsDifferenceInMotorPower)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 0, 0, 10000.0);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 0, 0, 10001.0);

    EXPECT_NE(a, b);
}

TEST(VehicleSignalEVTest, InequalityDetectsDifferenceInRegenPower)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 0, 0, 0, 5000.0);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 0, 0, 0, 0, 0, 5001.0);

    EXPECT_NE(a, b);
}

TEST(VehicleSignalEVTest, EqualityWorksWithZeroEVFields)
{
    // Two signals with identical base fields and zero EV fields should be equal
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345);

    EXPECT_EQ(a, b);
}

// ================================================
// Combined EV Telemetry Scenario Tests
// ================================================

TEST(VehicleSignalEVTest, RepresentsEVTelemetryWithMotoringPower)
{
    // Typical EV motoring scenario: motor drawing power
    const VehicleSignal signal(
        60.0,          // 60% throttle
        80.0,          // 80 km/h
        0.3,           // accelerating at 0.3g
        0.0,           // no braking
        123456789ULL,  // timestamp
        0.0,           // steering centered
        6000.0,        // motor at 6000 RPM
        380.0,         // 380V bus voltage
        35.0,          // 35A current draw
        13300.0,       // ~13.3kW power draw (will clamp to 20kW max)
        0.0            // no regen
    );

    // Motor power should clamp to verified max (20000W)
    EXPECT_DOUBLE_EQ(signal.getMotorPower(), 13300.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 6000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 380.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 35.0);
    EXPECT_DOUBLE_EQ(signal.getRegenPower(), 0.0);
}

TEST(VehicleSignalEVTest, RepresentsEVTelemetryWithRegen)
{
    // Typical EV regen scenario: motor generating power
    const VehicleSignal signal(
        0.0,           // 0% throttle
        60.0,          // 60 km/h decelerating
        -0.5,          // decelerating at 0.5g
        40.0,          // 40% brake pedal
        123456789ULL,  // timestamp
        0.0,           // steering centered
        4000.0,        // motor at 4000 RPM
        0.0,           // no voltage (regen mode)
        0.0,           // no current (regen mode)
        0.0,           // no motor power
        8000.0         // 8kW regen power
    );

    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 4000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorPower(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getRegenPower(), 8000.0);
}
