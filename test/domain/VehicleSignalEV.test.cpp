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
    const VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});

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
    const VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL,
        .throttlePercent = 50.0, .speedKmh = 100.0, .accelerationG = 0.5, .brakePercent = 25.0,
        .steeringAngleDeg = 45.0, .motorRpm = 5000.0, .motorHvVoltage = 400.0,
        .motorHvCurrent = 25.0, .motorTorqueNm = 350.0, .gearSelector = 4097});

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
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorRpm = 5000.0});
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 5000.0);
}

TEST(VehicleSignalEVTest, StoresMotorRpmOutOfRange)
{
    const VehicleSignal low(VehicleSignal::Params{.timestampUtcMs = 0, .motorRpm = -1000.0});
    EXPECT_DOUBLE_EQ(low.getMotorRpm().value(), -1000.0);

    const VehicleSignal high(VehicleSignal::Params{.timestampUtcMs = 0, .motorRpm = 25000.0});
    EXPECT_DOUBLE_EQ(high.getMotorRpm().value(), 25000.0);
}

TEST(VehicleSignalEVTest, AcceptsMotorRpmAtVerifiedMaximum)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorRpm = 20000.0});
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 20000.0);
}

// ================================================
// Motor Torque Tests (-7500..7500 from Tesla DIR_torqueActual)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorTorqueNm)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorTorqueNm = 350.0});
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 350.0);
}

TEST(VehicleSignalEVTest, StoresMotorTorqueOutOfRange)
{
    const VehicleSignal low(VehicleSignal::Params{.timestampUtcMs = 0, .motorTorqueNm = -8000.0});
    EXPECT_DOUBLE_EQ(low.getMotorTorqueNm().value(), -8000.0);

    const VehicleSignal high(VehicleSignal::Params{.timestampUtcMs = 0, .motorTorqueNm = 8000.0});
    EXPECT_DOUBLE_EQ(high.getMotorTorqueNm().value(), 8000.0);
}

TEST(VehicleSignalEVTest, NegativeTorqueRepresentsRegen)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorTorqueNm = -200.0});
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), -200.0);
}

// ================================================
// Gear Selector Tests
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesGearSelector)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .gearSelector = 4097});
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}

TEST(VehicleSignalEVTest, GearSelectorDefaultsToNullopt)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0});
    EXPECT_FALSE(signal.getGearSelector().has_value());
}

// ================================================
// Motor/HV Voltage Tests (0-1000V from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorHvVoltage)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorHvVoltage = 400.0});
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage().value(), 400.0);
}

TEST(VehicleSignalEVTest, StoresMotorHvVoltageOutOfRange)
{
    const VehicleSignal low(VehicleSignal::Params{.timestampUtcMs = 0, .motorHvVoltage = -100.0});
    EXPECT_DOUBLE_EQ(low.getMotorHvVoltage().value(), -100.0);

    const VehicleSignal high(VehicleSignal::Params{.timestampUtcMs = 0, .motorHvVoltage = 1200.0});
    EXPECT_DOUBLE_EQ(high.getMotorHvVoltage().value(), 1200.0);
}

// ================================================
// Motor/HV Current Tests (0-50A from Tesla CMPD)
// ================================================

TEST(VehicleSignalEVTest, StoresAndRetrievesMotorHvCurrent)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorHvCurrent = 25.0});
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent().value(), 25.0);
}

TEST(VehicleSignalEVTest, StoresMotorHvCurrentOutOfRange)
{
    const VehicleSignal low(VehicleSignal::Params{.timestampUtcMs = 0, .motorHvCurrent = -10.0});
    EXPECT_DOUBLE_EQ(low.getMotorHvCurrent().value(), -10.0);

    const VehicleSignal high(VehicleSignal::Params{.timestampUtcMs = 0, .motorHvCurrent = 75.0});
    EXPECT_DOUBLE_EQ(high.getMotorHvCurrent().value(), 75.0);
}

// ================================================
// Equality/Inequality Tests
// ================================================

TEST(VehicleSignalEVTest, EqualityIncludesAllEVFields)
{
    const VehicleSignal a(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 45.0,
        .motorRpm = 5000.0, .motorHvVoltage = 400.0, .motorHvCurrent = 25.0,
        .motorTorqueNm = 350.0, .gearSelector = 4097});
    const VehicleSignal b(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 45.0,
        .motorRpm = 5000.0, .motorHvVoltage = 400.0, .motorHvCurrent = 25.0,
        .motorTorqueNm = 350.0, .gearSelector = 4097});
    const VehicleSignal c(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 45.0,
        .motorRpm = 5000.0, .motorHvVoltage = 400.0, .motorHvCurrent = 25.0,
        .motorTorqueNm = 350.0, .gearSelector = -1});

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalEVTest, EqualityWorksWithZeroEVFields)
{
    const VehicleSignal a(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});
    const VehicleSignal b(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});

    EXPECT_EQ(a, b);
}

// ================================================
// Combined EV Telemetry Scenario
// ================================================

TEST(VehicleSignalEVTest, RepresentsEVTelemetryUnderAcceleration)
{
    const VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL,
        .throttlePercent = 60.0, .speedKmh = 80.0, .accelerationG = 0.3, .brakePercent = 0.0,
        .steeringAngleDeg = 0.0, .motorRpm = 6000.0, .motorHvVoltage = 380.0,
        .motorHvCurrent = 35.0, .motorTorqueNm = 350.0, .gearSelector = 4097});

    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 6000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvVoltage().value(), 380.0);
    EXPECT_DOUBLE_EQ(signal.getMotorHvCurrent().value(), 35.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 350.0);
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}

TEST(VehicleSignalEVTest, RepresentsEVTelemetryUnderRegen)
{
    const VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL,
        .throttlePercent = 0.0, .speedKmh = 60.0, .accelerationG = -0.5, .brakePercent = 40.0,
        .steeringAngleDeg = 0.0, .motorRpm = 4000.0,
        .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = -200.0, .gearSelector = 4097});

    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 4000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), -200.0);
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
}
