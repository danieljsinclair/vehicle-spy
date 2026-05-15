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
    const VehicleSignal signal(50.0, 100.0, 0.5, 25.0, 123456789ULL);

    EXPECT_DOUBLE_EQ(signal.getThrottlePercent(), 50.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getAccelerationG(), 0.5);
    EXPECT_DOUBLE_EQ(signal.getBrakePercent(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);

    // EV fields should default to zero for backward compatibility
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 0.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 0.0);
    EXPECT_EQ(signal.getGearSelector(), "");
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
        350.0,         // motorTorqueNm
        "D"            // gearSelector
    );

    EXPECT_DOUBLE_EQ(signal.getSteeringAngleDeg(), 45.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 5000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 400.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 25.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 350.0);
    EXPECT_EQ(signal.getGearSelector(), "D");
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
    const VehicleSignal low(0, 0, 0, 0, 0, 0, -1000.0);
    EXPECT_DOUBLE_EQ(low.getMotorRpm(), 0.0);

    const VehicleSignal high(0, 0, 0, 0, 0, 0, 25000.0);
    EXPECT_DOUBLE_EQ(high.getMotorRpm(), 20000.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorRpmAtVerifiedMaximum)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 20000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 20000.0);
}

// ================================================
// Motor Torque Tests (-7500..7500 from Tesla DIR_torqueActual)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorTorqueNm)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, 350.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 350.0);
}

TEST(VehicleSignalEVTest, ClampsMotorTorqueToVerifiedRange)
{
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, 0, 0, -8000.0);
    EXPECT_DOUBLE_EQ(low.getMotorTorqueNm(), -7500.0);

    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 0, 0, 8000.0);
    EXPECT_DOUBLE_EQ(high.getMotorTorqueNm(), 7500.0);
}

TEST(VehicleSignalEVTest, NegativeTorqueRepresentsRegen)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, -200.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), -200.0);
}

// ================================================
// Gear Selector Tests
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesGearSelector)
{
    const VehicleSignal signal(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "D");
    EXPECT_EQ(signal.getGearSelector(), "D");
}

TEST(VehicleSignalEVTest, GearSelectorDefaultsToEmpty)
{
    const VehicleSignal signal(0, 0, 0, 0, 0);
    EXPECT_EQ(signal.getGearSelector(), "");
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
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, -100.0);
    EXPECT_DOUBLE_EQ(low.getMotorHvVoltage(), 0.0);

    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 1200.0);
    EXPECT_DOUBLE_EQ(high.getMotorHvVoltage(), 1000.0);
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
    const VehicleSignal low(0, 0, 0, 0, 0, 0, 0, 0, -10.0);
    EXPECT_DOUBLE_EQ(low.getMotorHvCurrent(), 0.0);

    const VehicleSignal high(0, 0, 0, 0, 0, 0, 0, 0, 75.0);
    EXPECT_DOUBLE_EQ(high.getMotorHvCurrent(), 50.0);
}

// ================================================
// Equality/Inequality Tests
// ================================================

TEST(VehicleSignalEVTest, EqualityIncludesAllEVFields)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345, 45.0, 5000.0, 400.0, 25.0, 350.0, "D");
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345, 45.0, 5000.0, 400.0, 25.0, 350.0, "D");
    const VehicleSignal c(50.0, 100.0, 0.5, 25.0, 12345, 45.0, 5000.0, 400.0, 25.0, 350.0, "R");

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalEVTest, EqualityWorksWithZeroEVFields)
{
    const VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345);
    const VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345);

    EXPECT_EQ(a, b);
}

// ================================================
// Combined EV Telemetry Scenario
// ================================================

TEST(VehicleSignalEVTest, RepresentsEVTelemetryUnderAcceleration)
{
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
        350.0,         // positive torque (motoring)
        "D"            // drive
    );

    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 6000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage(), 380.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent(), 35.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 350.0);
    EXPECT_EQ(signal.getGearSelector(), "D");
}

TEST(VehicleSignalEVTest, RepresentsEVTelemetryUnderRegen)
{
    const VehicleSignal signal(
        0.0,           // 0% throttle
        60.0,          // 60 km/h decelerating
        -0.5,          // decelerating at 0.5g
        40.0,          // 40% brake pedal
        123456789ULL,  // timestamp
        0.0,           // steering centered
        4000.0,        // motor at 4000 RPM
        0.0, 0.0,      // HV readings not available
        -200.0,        // negative torque (regen)
        "D"
    );

    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 4000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), -200.0);
    EXPECT_EQ(signal.getGearSelector(), "D");
}
