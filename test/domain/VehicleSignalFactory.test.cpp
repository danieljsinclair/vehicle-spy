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
    EXPECT_EQ(signal.getMotorRpm(), 2500.0);
}

TEST_F(VehicleSignalFactoryTest, BuildFromMultipleCanFramesWithMultipleSignals) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
    frames[297] = {0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm(), 2500.0);
    EXPECT_NEAR(signal.getThrottlePercent(), 1.2, 0.01);
    EXPECT_NEAR(signal.getSteeringAngleDeg(), -614.4, 0.01);
}

TEST_F(VehicleSignalFactoryTest, UnmappedSignalsDefaultToZero) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getSpeedKmh(), 0.0);
    EXPECT_EQ(signal.getAccelerationG(), 0.0);
    EXPECT_EQ(signal.getMotorHvVoltage(), 0.0);
    EXPECT_EQ(signal.getMotorHvCurrent(), 0.0);
    EXPECT_EQ(signal.getMotorTorqueNm(), 0.0);
}

TEST_F(VehicleSignalFactoryTest, MissingCanFramesProduceDefaultValues) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm(), 2500.0);
    EXPECT_EQ(signal.getThrottlePercent(), 0.0);
    EXPECT_EQ(signal.getSteeringAngleDeg(), 0.0);
    EXPECT_EQ(signal.getBrakePercent(), 0.0);
}

TEST_F(VehicleSignalFactoryTest, FullIntegrationRealTeslaDBCPatterns) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00};
    frames[297] = {0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm(), 2500.0);
    EXPECT_NEAR(signal.getThrottlePercent(), 40.0, 0.01);
    EXPECT_NEAR(signal.getSteeringAngleDeg(), -614.4, 0.01);
}

TEST_F(VehicleSignalFactoryTest, BuildFromEmptyFramesReturnsDefaultSignal) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getTimestampUtcMs(), 1234567890);
    EXPECT_EQ(signal.getThrottlePercent(), 0.0);
    EXPECT_EQ(signal.getSpeedKmh(), 0.0);
    EXPECT_EQ(signal.getAccelerationG(), 0.0);
    EXPECT_EQ(signal.getBrakePercent(), 0.0);
    EXPECT_EQ(signal.getSteeringAngleDeg(), 0.0);
    EXPECT_EQ(signal.getMotorRpm(), 0.0);
    EXPECT_EQ(signal.getMotorHvVoltage(), 0.0);
    EXPECT_EQ(signal.getMotorHvCurrent(), 0.0);
    EXPECT_EQ(signal.getMotorTorqueNm(), 0.0);
}

TEST_F(VehicleSignalFactoryTest, SignalWithNegativeTorqueValue) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x04, 0xE0, 0xFF, 0xFF, 0xFF};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm(), 0.0);
    EXPECT_EQ(signal.getMotorTorqueNm(), -2048.0);
}
