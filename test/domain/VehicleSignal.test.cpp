#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// VehicleSignal Unit Tests
// TDD - Full test coverage for value object
// ================================================

TEST(VehicleSignalTest, ConstructsWithValidValues)
{
    const VehicleSignal signal(123456789ULL, 50.0, 100.0, 0.5, 25.0);

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
    const VehicleSignal signal(123456789ULL);

    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    EXPECT_FALSE(signal.getBrakePercent().has_value());
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
}

TEST(VehicleSignalTest, ValueEqualityWorks)
{
    const VehicleSignal a(12345, 50.0, 100.0, 0.5, 25.0);
    const VehicleSignal b(12345, 50.0, 100.0, 0.5, 25.0);
    const VehicleSignal c(12345, 51.0, 100.0, 0.5, 25.0);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VehicleSignalTest, ValueInequalityWorks)
{
    const VehicleSignal a(12345, 50.0, 100.0, 0.5, 25.0);
    const VehicleSignal b(12345, 51.0, 100.0, 0.5, 25.0);

    EXPECT_NE(a, b);
}

TEST(VehicleSignalTest, OptionalFieldsAreIndependent)
{
    const VehicleSignal signal(12345, std::nullopt, 100.0, std::nullopt, 25.0);

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
    VehicleSignal signal(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, 15000.5);
    ASSERT_TRUE(signal.getMotorRpm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorRpm().value(), 15000.5);
}

TEST(VehicleSignalTest, MotorRpmDefaultsToNullopt)
{
    VehicleSignal signal(0);
    EXPECT_FALSE(signal.getMotorRpm().has_value());
}

TEST(VehicleSignalTest, GearSelectorStoresValidValues) {
    VehicleSignal signalP(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::optional<std::int32_t>(-2));
    VehicleSignal signalR(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::optional<std::int32_t>(-1));
    VehicleSignal signalN(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::optional<std::int32_t>(0));
    VehicleSignal signalD(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::optional<std::int32_t>(4097));
    VehicleSignal signalS(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                          std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::optional<std::int32_t>(4098));

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
    VehicleSignal signal(0);
    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST(VehicleSignalTest, MotorTorqueNmStoresValue) {
    VehicleSignal signal(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt, 375.5);
    ASSERT_TRUE(signal.getMotorTorqueNm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), 375.5);
}

TEST(VehicleSignalTest, MotorTorqueNmAcceptsNegativeValues) {
    VehicleSignal signal(0, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt, -250.5);
    ASSERT_TRUE(signal.getMotorTorqueNm().has_value());
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm().value(), -250.5);
}

TEST(VehicleSignalTest, MotorTorqueNmDefaultsToNullopt) {
    VehicleSignal signal(0);
    EXPECT_FALSE(signal.getMotorTorqueNm().has_value());
}

TEST(VehicleSignalTest, EqualityIncludesNewFields) {
    VehicleSignal a(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 3500.0, 0.0, 0.0, 150.0, std::optional<std::int32_t>(4097));
    VehicleSignal b(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 3500.0, 0.0, 0.0, 150.0, std::optional<std::int32_t>(4097));
    VehicleSignal c(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 4000.0, 0.0, 0.0, 150.0, std::optional<std::int32_t>(4097)); // different rpm
    VehicleSignal d(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 3500.0, 0.0, 0.0, 150.0, std::optional<std::int32_t>(0)); // different gear
    VehicleSignal e(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 3500.0, 0.0, 0.0, 200.0, std::optional<std::int32_t>(4097)); // different torque

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(a, e);
}

TEST(VehicleSignalTest, InequalityIncludesNewFields) {
    VehicleSignal a(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 3500.0, 0.0, 0.0, 150.0, std::optional<std::int32_t>(4097));
    VehicleSignal b(12345ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 3500.0, 0.0, 0.0, 150.0, std::optional<std::int32_t>(0));

    EXPECT_NE(a, b);
}

TEST(VehicleSignalTest, AllFieldsConstructCorrectly) {
    VehicleSignal signal(123456789ULL, 50.0, 100.0, 0.5, 25.0, 0.0, 5000.0, 0.0, 0.0, 300.0, std::optional<std::int32_t>(4097));

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
