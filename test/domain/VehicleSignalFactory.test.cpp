#include <gtest/gtest.h>
#include <memory>
#include "vehicle-sim/domain/VehicleSignalFactory.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"

using namespace vehicle_sim::domain;

class VehicleSignalFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = std::make_unique<VehicleConfig>(
            "tesla_model3.dbc",
            "Tesla Model Y",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DIR_torqueActual", "motorTorqueNm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"DI_brakePedal", "brakePercent"},
                {"SteeringAngle129", "steeringAngleDeg"}
            }
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

TEST_F(VehicleSignalFactoryTest, GearSelectorMapsZeroToPark) {
    auto configWithGear = std::make_unique<VehicleConfig>(
        "test.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{
            {"Transmission_Gear", "gearSelector"}
        },
        "",  // canBus
        false,  // isCANProtocol
        std::unordered_map<int, std::string>{{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );

    DBCParseResult gearParseResult;
    gearParseResult.signalsByCanId[256].emplace_back(DBCSignalParams{
        256, "Transmission_Gear", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 10.0
    });

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[256] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getGearSelector().value(), "P");
}

TEST_F(VehicleSignalFactoryTest, GearSelectorMapsOneToReverse) {
    auto configWithGear = std::make_unique<VehicleConfig>(
        "test.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{
            {"Transmission_Gear", "gearSelector"}
        },
        "",  // canBus
        false,  // isCANProtocol
        std::unordered_map<int, std::string>{{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );

    DBCParseResult gearParseResult;
    gearParseResult.signalsByCanId[256].emplace_back(DBCSignalParams{
        256, "Transmission_Gear", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 10.0
    });

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[256] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getGearSelector().value(), "R");
}

TEST_F(VehicleSignalFactoryTest, GearSelectorMapsTwoToNeutral) {
    auto configWithGear = std::make_unique<VehicleConfig>(
        "test.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{
            {"Transmission_Gear", "gearSelector"}
        },
        "",  // canBus
        false,  // isCANProtocol
        std::unordered_map<int, std::string>{{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );

    DBCParseResult gearParseResult;
    gearParseResult.signalsByCanId[256].emplace_back(DBCSignalParams{
        256, "Transmission_Gear", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 10.0
    });

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[256] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getGearSelector().value(), "N");
}

TEST_F(VehicleSignalFactoryTest, GearSelectorMapsThreeToDrive) {
    auto configWithGear = std::make_unique<VehicleConfig>(
        "test.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{
            {"Transmission_Gear", "gearSelector"}
        },
        "",  // canBus
        false,  // isCANProtocol
        std::unordered_map<int, std::string>{{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );

    DBCParseResult gearParseResult;
    gearParseResult.signalsByCanId[256].emplace_back(DBCSignalParams{
        256, "Transmission_Gear", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 10.0
    });

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[256] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getGearSelector().value(), "D");
}

TEST_F(VehicleSignalFactoryTest, GearSelectorMapsFourToSport) {
    auto configWithGear = std::make_unique<VehicleConfig>(
        "test.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{
            {"Transmission_Gear", "gearSelector"}
        },
        "",  // canBus
        false,  // isCANProtocol
        std::unordered_map<int, std::string>{{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );

    DBCParseResult gearParseResult;
    gearParseResult.signalsByCanId[256].emplace_back(DBCSignalParams{
        256, "Transmission_Gear", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 10.0
    });

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[256] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getGearSelector().value(), "S");
}

TEST_F(VehicleSignalFactoryTest, GearSelectorMapsUnknownToEmpty) {
    auto configWithGear = std::make_unique<VehicleConfig>(
        "test.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{
            {"Transmission_Gear", "gearSelector"}
        },
        "",  // canBus
        false,  // isCANProtocol
        std::unordered_map<int, std::string>{{0, "P"}, {1, "R"}, {2, "N"}, {3, "D"}, {4, "S"}}  // gearCodeMappings
    );

    DBCParseResult gearParseResult;
    gearParseResult.signalsByCanId[256].emplace_back(DBCSignalParams{
        256, "Transmission_Gear", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 10.0
    });

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[256] = {0x99, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST_F(VehicleSignalFactoryTest, GearSelectorDefaultsToNulloptWhenNotMapped) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}
