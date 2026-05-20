#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// VehicleSignal EV Fields Extension Tests
// Fields sourced from verified DBC data:
//   - joshwardell/model3dbc Model3CAN.dbc (Tesla Model 3/Y)
//   - commaai/opendbc vw_mqbevo.dbc (Audi e-tron 2021)
//
// Motor/Drivetrain (Tesla CAN 264):
//   - Motor RPM (DI_axleSpeed): 0-20000 RPM
//   - Motor torque (DI_torqueActual): -7500..7500 Nm
//
// Steering (Tesla CAN 129/297):
//   - Steering angle: -819.2..819.2 deg
// ================================================

// ================================================
// Backward Compatibility Tests
// ================================================

TEST(VehicleSignalEVTest, ConstructsWithDefaultsForEVFields)
{
    const VehicleSignal signal(123456789ULL, 50.0, 100.0, 0.5, 25.0);

    EXPECT_DOUBLE_EQ(signal.getThrottlePercent().value(), 50.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh().value(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getAccelerationG().value(), 0.5);
    EXPECT_DOUBLE_EQ(signal.getBrakePercent().value(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);

    // EV fields should default to nullopt when not provided
    EXPECT_FALSE(signal.getMotorRpm().has_value());
    EXPECT_FALSE(signal.getMotorHvVoltage().has_value());
    EXPECT_FALSE(signal.getMotorHvCurrent().has_value());
    EXPECT_FALSE(signal.getMotorTorqueNm().has_value());
    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST(VehicleSignalEVTest, ConstructsWithExplicitEVFields)
{
    const VehicleSignal signal(
        123456789ULL,  // timestampUtcMs
        50.0,          // throttlePercent
        100.0,         // speedKmh
        0.5,           // accelerationG
        25.0,          // brakePercent
        45.0,          // steeringAngleDeg
        5000.0,        // motorRpm
        400.0,         // motorHvVoltage
        25.0,          // motorHvCurrent
        350.0,         // motorTorqueNm
        4097           // gearSelector
    );

    EXPECT_DOUBLE_EQ(signal.getSteeringAngleDeg().value(), 45.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 5000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage().value(), 400.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent().value(), 25.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 350.0);
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}

// ================================================
// Motor RPM Tests (0-20000 from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorRpm)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, 5000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 5000.0);
}

TEST(VehicleSignalEVTest, StoresMotorRpmOutOfRange)
{
    const VehicleSignal low(0, {}, {}, {}, {}, {}, -1000.0);
    EXPECT_DOUBLE_EQ(low.getMotorRpm().value(), -1000.0);

    const VehicleSignal high(0, {}, {}, {}, {}, {}, 25000.0);
    EXPECT_DOUBLE_EQ(high.getMotorRpm().value(), 25000.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorRpmAtVerifiedMaximum)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, 20000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 20000.0);
}

// ================================================
// Motor Torque Tests (-7500..7500 from Tesla DIR_torqueActual)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorTorqueNm)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, {}, {}, {}, 350.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 350.0);
}

TEST(VehicleSignalEVTest, StoresMotorTorqueOutOfRange)
{
    const VehicleSignal low(0, {}, {}, {}, {}, {}, {}, {}, {}, -8000.0);
    EXPECT_DOUBLE_EQ(low.getMotorTorqueNm().value(), -8000.0);

    const VehicleSignal high(0, {}, {}, {}, {}, {}, {}, {}, {}, 8000.0);
    EXPECT_DOUBLE_EQ(high.getMotorTorqueNm().value(), 8000.0);
}

TEST(VehicleSignalEVTest, NegativeTorqueRepresentsRegen)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, {}, {}, {}, -200.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), -200.0);
}

// ================================================
// Gear Selector Tests
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesGearSelector)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, {}, {}, {}, {}, 4097);
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}

TEST(VehicleSignalEVTest, GearSelectorDefaultsToNullopt)
{
    const VehicleSignal signal(0);
    EXPECT_FALSE(signal.getGearSelector().has_value());
}

// ================================================
// Motor/HV Voltage Tests (0-1000V from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorHvVoltage)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, {}, 400.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage().value(), 400.0);
}

TEST(VehicleSignalEVTest, StoresMotorHvVoltageOutOfRange)
{
    const VehicleSignal low(0, {}, {}, {}, {}, {}, {}, -100.0);
    EXPECT_DOUBLE_EQ(low.getMotorHvVoltage().value(), -100.0);

    const VehicleSignal high(0, {}, {}, {}, {}, {}, {}, 1200.0);
    EXPECT_DOUBLE_EQ(high.getMotorHvVoltage().value(), 1200.0);
}

// ================================================
// Motor/HV Current Tests (0-50A from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorHvCurrent)
{
    const VehicleSignal signal(0, {}, {}, {}, {}, {}, {}, {}, 25.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent().value(), 25.0);
}

TEST(VehicleSignalEVTest, StoresMotorHvCurrentOutOfRange)
{
    const VehicleSignal low(0, {}, {}, {}, {}, {}, {}, {}, -10.0);
    EXPECT_DOUBLE_EQ(low.getMotorHvCurrent().value(), -10.0);

    const VehicleSignal high(0, {}, {}, {}, {}, {}, {}, {}, 75.0);
    EXPECT_DOUBLE_EQ(high.getMotorHvCurrent().value(), 75.0);
}

// ================================================
// Equality/Inequality Tests
// ================================================

TEST(VehicleSignalEVTest, EqualityIncludesAllEVFields)
{
    const VehicleSignal a(12345, 50.0, 100.0, 0.5, 25.0, 45.0, 5000.0, 400.0, 25.0, 350.0, 4097);
    const VehicleSignal b(12345, 50.0, 100.0, 0.5, 25.0, 45.0, 5000.0, 400.0, 25.0, 350.0, 4097);
    const VehicleSignal c(12345, 50.0, 100.0, 0.5, 25.0, 45.0, 5000.0, 400.0, 25.0, 350.0, -1);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalEVTest, EqualityWorksWithZeroEVFields)
{
    const VehicleSignal a(12345, 50.0, 100.0, 0.5, 25.0);
    const VehicleSignal b(12345, 50.0, 100.0, 0.5, 25.0);

    EXPECT_EQ(a, b);
}

// ================================================
// Combined EV Telemetry Scenario
// ================================================

TEST(VehicleSignalEVTest, RepresentsEVTelemetryUnderAcceleration)
{
    const VehicleSignal signal(
        123456789ULL,  // timestamp
        60.0,          // 60% throttle
        80.0,          // 80 km/h
        0.3,           // accelerating at 0.3g
        0.0,           // no braking
        0.0,           // steering centered
        6000.0,        // motor at 6000 RPM
        380.0,         // 380V bus voltage
        35.0,          // 35A current draw
        350.0,         // positive torque (motoring)
        4097           // drive
    );

    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 6000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage().value(), 380.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent().value(), 35.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 350.0);
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}

TEST(VehicleSignalEVTest, RepresentsEVTelemetryUnderRegen)
{
    const VehicleSignal signal(
        123456789ULL,  // timestamp
        0.0,           // 0% throttle
        60.0,          // 60 km/h decelerating
        -0.5,          // decelerating at 0.5g
        40.0,          // 40% brake pedal
        0.0,           // steering centered
        4000.0,        // motor at 4000 RPM
        0.0, 0.0,      // HV readings not available
        -200.0,        // negative torque (regen)
        4097
    );

    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 4000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), -200.0);
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}
