#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// VehicleSignal Unit Tests
// TDD - Full test coverage for value object
// ================================================

TEST(VehicleSignalTest, ConstructsWithValidValues)
{
    const VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL,
        .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});

    ASSERT_TRUE(signal.getThrottlePercent().has_value());
    EXPECT_DOUBLE_EQ(signal.getThrottlePercent().value(), 50.0);
    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh().value(), 100.0);
    ASSERT_TRUE(signal.getAccelerationG().has_value());
    EXPECT_DOUBLE_EQ(signal.getAccelerationG().value(), 0.5);
    ASSERT_TRUE(signal.getBrakePercent().has_value());
    EXPECT_DOUBLE_EQ(signal.getBrakePercent().value(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
}

TEST(VehicleSignalTest, ConstructsWithOptionalNullopt)
{
    const VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 123456789ULL});

    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    EXPECT_FALSE(signal.getBrakePercent().has_value());
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
}

TEST(VehicleSignalTest, ValueEqualityWorks)
{
    const VehicleSignal a(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});
    const VehicleSignal b(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});
    const VehicleSignal c(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 51.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalTest, ValueInequalityWorks)
{
    const VehicleSignal a(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});
    const VehicleSignal b(VehicleSignal::Params{
        .timestampUtcMs = 12345, .throttlePercent = 51.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0});

    EXPECT_NE(a, b);
}

TEST(VehicleSignalTest, OptionalFieldsAreIndependent)
{
    const VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 12345, .speedKmh = 100.0, .brakePercent = 25.0});

    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh().value(), 100.0);
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    ASSERT_TRUE(signal.getBrakePercent().has_value());
    EXPECT_DOUBLE_EQ(signal.getBrakePercent().value(), 25.0);
}

// ================================================
// VehicleSignal Extended Field Tests
// Tests for EV-specific fields (motorRpm, motorTorqueNm, gearSelector, etc.)
// ================================================

TEST(VehicleSignalTest, MotorRpmStoresValue)
{
    VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorRpm = 15000.5});
    ASSERT_TRUE(signal.getMotorRpm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 15000.5);
}

TEST(VehicleSignalTest, MotorRpmDefaultsToNullopt)
{
    VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0});
    EXPECT_FALSE(signal.getMotorRpm().has_value());
}

TEST(VehicleSignalTest, GearSelectorStoresValidValues) {
    VehicleSignal signalP(VehicleSignal::Params{.timestampUtcMs = 0, .gearSelector = -2});
    VehicleSignal signalR(VehicleSignal::Params{.timestampUtcMs = 0, .gearSelector = -1});
    VehicleSignal signalN(VehicleSignal::Params{.timestampUtcMs = 0, .gearSelector = 0});
    VehicleSignal signalD(VehicleSignal::Params{.timestampUtcMs = 0, .gearSelector = 4097});
    VehicleSignal signalS(VehicleSignal::Params{.timestampUtcMs = 0, .gearSelector = 4098});

    ASSERT_TRUE(signalP.getGearSelector().has_value());
    EXPECT_EQ(signalP.getGearSelector().value(), -2);
    ASSERT_TRUE(signalR.getGearSelector().has_value());
    EXPECT_EQ(signalR.getGearSelector().value(), -1);
    ASSERT_TRUE(signalN.getGearSelector().has_value());
    EXPECT_EQ(signalN.getGearSelector().value(), 0);
    ASSERT_TRUE(signalD.getGearSelector().has_value());
    EXPECT_EQ(signalD.getGearSelector().value(), 4097);
    ASSERT_TRUE(signalS.getGearSelector().has_value());
    EXPECT_EQ(signalS.getGearSelector().value(), 4098);
}

TEST(VehicleSignalTest, GearSelectorDefaultsToNullopt) {
    VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0});
    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST(VehicleSignalTest, MotorTorqueNmStoresValue) {
    VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorTorqueNm = 375.5});
    ASSERT_TRUE(signal.getMotorTorqueNm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 375.5);
}

TEST(VehicleSignalTest, MotorTorqueNmAcceptsNegativeValues) {
    VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0, .motorTorqueNm = -250.5});
    ASSERT_TRUE(signal.getMotorTorqueNm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), -250.5);
}

TEST(VehicleSignalTest, MotorTorqueNmDefaultsToNullopt) {
    VehicleSignal signal(VehicleSignal::Params{.timestampUtcMs = 0});
    EXPECT_FALSE(signal.getMotorTorqueNm().has_value());
}

TEST(VehicleSignalTest, EqualityIncludesNewFields) {
    VehicleSignal a(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 3500.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 150.0, .gearSelector = 4097});
    VehicleSignal b(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 3500.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 150.0, .gearSelector = 4097});
    VehicleSignal c(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 4000.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 150.0, .gearSelector = 4097}); // different rpm
    VehicleSignal d(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 3500.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 150.0, .gearSelector = 0}); // different gear
    VehicleSignal e(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 3500.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 200.0, .gearSelector = 4097}); // different torque

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(a, e);
}

TEST(VehicleSignalTest, InequalityIncludesNewFields) {
    VehicleSignal a(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 3500.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 150.0, .gearSelector = 4097});
    VehicleSignal b(VehicleSignal::Params{
        .timestampUtcMs = 12345ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 3500.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 150.0, .gearSelector = 0});

    EXPECT_NE(a, b);
}

TEST(VehicleSignalTest, AllFieldsConstructCorrectly) {
    VehicleSignal signal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL, .throttlePercent = 50.0, .speedKmh = 100.0,
        .accelerationG = 0.5, .brakePercent = 25.0, .steeringAngleDeg = 0.0,
        .motorRpm = 5000.0, .motorHvVoltage = 0.0, .motorHvCurrent = 0.0,
        .motorTorqueNm = 300.0, .gearSelector = 4097});

    ASSERT_TRUE(signal.getThrottlePercent().has_value());
    EXPECT_DOUBLE_EQ(signal.getThrottlePercent().value(), 50.0);
    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh().value(), 100.0);
    ASSERT_TRUE(signal.getAccelerationG().has_value());
    EXPECT_DOUBLE_EQ(signal.getAccelerationG().value(), 0.5);
    ASSERT_TRUE(signal.getBrakePercent().has_value());
    EXPECT_DOUBLE_EQ(signal.getBrakePercent().value(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
    ASSERT_TRUE(signal.getMotorRpm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 5000.0);
    ASSERT_TRUE(signal.getGearSelector().has_value());
    EXPECT_EQ(signal.getGearSelector().value(), 4097);
    ASSERT_TRUE(signal.getMotorTorqueNm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 300.0);
}
