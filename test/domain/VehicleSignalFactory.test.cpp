#include <gtest/gtest.h>
#include <memory>
#include "vehicle-sim/domain/VehicleSignalFactory.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

class VehicleSignalFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = std::make_unique<VehicleConfig>(
            "tesla_model3.dbc",
            "tesla_model3.dbc",
            "Tesla Model Y",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DIR_torqueActual", "motorTorqueNm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"DI_brakePedal", "brakePercent"},
                {"SteeringAngle129", "steeringAngleDeg"}
            },
            "",  // canBus
            false  // isCANProtocol
        );

        parseResult_.signalsByCanId[264].emplace_back(DBCSignalParams{
            264, "DIR_axleSpeed", 40, 16, DBCByteOrder::Intel, 0.1, 0.0, true, "RPM", -2750.0, 2750.0
        });
        parseResult_.signalsByCanId[264].emplace_back(DBCSignalParams{
            264, "DIR_torqueActual", 27, 13, DBCByteOrder::Intel, 2.0, 0.0, true, "Nm", -7500.0, 7500.0
        });
        parseResult_.signalsByCanId[280].emplace_back(DBCSignalParams{
            280, "DI_accelPedalPos", 32, 8, DBCByteOrder::Motorola, 0.4, 0.0, false, "%", 0.0, 100.0
        });
        parseResult_.signalsByCanId[297].emplace_back(DBCSignalParams{
            297, "SteeringAngle129", 16, 14, DBCByteOrder::Motorola, 0.1, -819.2, false, "deg", -819.2, 819.1
        });
    }

    // Helper to create Tesla DI_gear signal with value table
    void addTeslaGearSignal(DBCParseResult& result) {
        result.signalsByCanId[280].emplace_back(DBCSignalParams{
            280, "DI_gear", 12, 3, DBCByteOrder::Intel, 1.0, 0.0, true, "", 0.0, 7.0,
            {{0, "DI_GEAR_INVALID"},
             {1, "DI_GEAR_P"},
             {2, "DI_GEAR_R"},
             {3, "DI_GEAR_N"},
             {4, "DI_GEAR_D"},
             {7, "DI_GEAR_SNA"}}
        });
    }

    std::unique_ptr<VehicleConfig> config_;
    DBCParseResult parseResult_;
};

TEST_F(VehicleSignalFactoryTest, BuildFromSingleCanFrameWithOneMappedSignal) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getTimestampUtcMs(), 1234567890);
    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
}

TEST_F(VehicleSignalFactoryTest, BuildFromMultipleCanFramesWithMultipleSignals) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
    frames[297] = {0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(signal.getThrottlePercent().value(), 1.2, 0.01);
    EXPECT_NEAR(signal.getSteeringAngleDeg().value(), -614.4, 0.01);
}

TEST_F(VehicleSignalFactoryTest, UnmappedSignalsDefaultToNullopt) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    // speedKmh, accelerationG, motorHvVoltage, motorHvCurrent have no signal mappings
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    EXPECT_FALSE(signal.getMotorHvVoltage().has_value());
    EXPECT_FALSE(signal.getMotorHvCurrent().has_value());
    // motorTorqueNm IS mapped (DIR_torqueActual), CAN 264 provides raw 0 -> value 0.0
    EXPECT_TRUE(signal.getMotorTorqueNm().has_value());
}

TEST_F(VehicleSignalFactoryTest, MissingCanFramesProduceDefaultNullopt) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSteeringAngleDeg().has_value());
    EXPECT_FALSE(signal.getBrakePercent().has_value());
}

TEST_F(VehicleSignalFactoryTest, FullIntegrationRealTeslaDBCPatterns) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00};
    frames[297] = {0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(signal.getThrottlePercent().value(), 40.0, 0.01);
    EXPECT_NEAR(signal.getSteeringAngleDeg().value(), -614.4, 0.01);
}

TEST_F(VehicleSignalFactoryTest, BuildFromEmptyFramesReturnsDefaultSignal) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getTimestampUtcMs(), 1234567890);
    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    EXPECT_FALSE(signal.getBrakePercent().has_value());
    EXPECT_FALSE(signal.getSteeringAngleDeg().has_value());
    EXPECT_FALSE(signal.getMotorRpm().has_value());
    EXPECT_FALSE(signal.getMotorHvVoltage().has_value());
    EXPECT_FALSE(signal.getMotorHvCurrent().has_value());
    EXPECT_FALSE(signal.getMotorTorqueNm().has_value());
}

TEST_F(VehicleSignalFactoryTest, SignalWithNegativeTorqueValue) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x04, 0xE0, 0xFF, 0xFF, 0xFF};

    auto signal = factory.build(frames, 1234567890);

    // motorRpm IS mapped (DIR_axleSpeed), so it has a value from this frame
    EXPECT_TRUE(signal.getMotorRpm().has_value());
    EXPECT_EQ(signal.getMotorTorqueNm().value(), -2048.0);
}

TEST_F(VehicleSignalFactoryTest, GearSelectorDefaultsToNulloptWhenNotMapped) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST_F(VehicleSignalFactoryTest, GearCodeFourMapsToAuto1) {
    // CAN 280 frame with DI_gear = 4 (Drive)
    // DI_gear: startBit=12, 3 bits, Intel, scale=1, offset=0
    // gear=4 = binary 100, at bits 12-14 (byte 1, bit 4)
    // byte 1 = 0x40 (binary 01000000)
    // Expected: gearSelector = Gear::AUTO_1 (0x1001)
    auto configWithGear = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_gear", "gearSelector"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult gearParseResult;
    addTeslaGearSignal(gearParseResult);

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[280] = {0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    ASSERT_TRUE(signal.getGearSelector().has_value());
    EXPECT_EQ(signal.getGearSelector().value(), Gear::AUTO_1);
}

TEST_F(VehicleSignalFactoryTest, AnalogSpeed500MapsToFiftyKmh) {
    // CAN 872 frame with DI_analogSpeed = 500
    // DI_analogSpeed: startBit=16, 12 bits, Intel, scale=0.1, offset=0
    // speedKmh = 500 * 0.1 = 50.0 km/h
    // raw value 500 = 0x01F4
    // At bit 16: byte 2 = 0xF4, byte 3 = 0x01
    auto configWithSpeed = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_analogSpeed", "speedKmh"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult speedParseResult;
    speedParseResult.signalsByCanId[872].emplace_back(DBCSignalParams{
        872, "DI_analogSpeed", 16, 12, DBCByteOrder::Intel, 0.1, 0.0, true, "speed", 0.0, 150.0
    });

    VehicleSignalFactory factory(*configWithSpeed, speedParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[872] = {0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    EXPECT_NEAR(signal.getSpeedKmh().value(), 50.0, 0.1);
}

TEST_F(VehicleSignalFactoryTest, GearCodeZeroReturnsNullopt) {
    // DI_gear = 0 (INVALID)
    // DBC VAL_ table defines 0 as "DI_GEAR_INVALID"
    // Expected: nullopt (invalid signals filtered out)
    auto configWithGear = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_gear", "gearSelector"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult gearParseResult;
    addTeslaGearSignal(gearParseResult);

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST_F(VehicleSignalFactoryTest, GearCodeSevenReturnsNullopt) {
    // DI_gear = 7 (SNA - Signal Not Available)
    // DBC VAL_ table defines 7 as "DI_GEAR_SNA"
    // Expected: nullopt (SNA signals filtered out)
    auto configWithGear = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_gear", "gearSelector"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult gearParseResult;
    addTeslaGearSignal(gearParseResult);

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    // gear=7 = binary 111 = 0xE0 at bit position 12 (byte 1, bits 4-6)
    frames[280] = {0x00, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}