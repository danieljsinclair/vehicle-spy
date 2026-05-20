#include <gtest/gtest.h>
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

class FrameReplayTest : public ::testing::Test {
protected:
    DBCFileParser parser;

    VehicleConfig createTeslaConfig() {
        return VehicleConfig(
            "Model3CAN.dbc",
            "Model3CAN.dbc",
            "Tesla Model 3",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DIR_torqueActual", "motorTorqueNm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"SteeringAngle129", "steeringAngleDeg"},
                {"DI_gear", "gearSelector"},
                {"DI_analogSpeed", "speedKmh"}
            },
            "",  // canBus
            true  // isCANProtocol
        );
    }
};

TEST_F(FrameReplayTest, ReplaySyntheticCanFrames_MultiFrameAccumulation) {
    // Step 1: Parse Tesla DBC
    auto parseResult = parser.parseFile("resources/dbc/Model3CAN.dbc");

    // Step 2: Create config with full signal mappings
    auto config = createTeslaConfig();

    // Step 3: Create translator
    DBCSignalTranslator translator(config, parseResult);

    // Step 4: Frame sequence - synthetic known-good frames
    // Frame 1: CAN 264 with motorRpm = 2500.0
    // DIR_axleSpeed: startBit=40, 16-bit, Intel, scale=0.1, offset=0
    // 2500.0 / 0.1 = 25000 = 0x61A8
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00, 0x00  // motorRpm = 2500.0
    };
    auto r1 = translator.translate(frame264);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->getMotorRpm().value(), 2500.0);

    // Frame 2: CAN 280 with gear=4 (Drive) and throttle=50%
    // DI_gear: startBit=12, 3 bits, gear=4 -> byte 1 = 0x40
    // DI_accelPedalPos: startBit=32, 8-bit, Intel, scale=0.4, offset=0
    // 50.0 / 0.4 = 125 = 0x7D
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x40, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00  // gear=4, throttle=50%
    };
    auto r2 = translator.translate(frame280);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getThrottlePercent().value(), 50.0, 0.1);
    // Previous motorRpm should persist
    EXPECT_EQ(r2->getMotorRpm().value(), 2500.0);

    // Frame 3: CAN 297 with steering angle = 0.0 deg
    // SteeringAngle129: startBit=16, 14-bit, Intel, scale=0.1, offset=-819.2
    // 0.0 / 0.1 + 819.2 = 8192 = 0x2000
    std::vector<std::uint8_t> frame297 = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00  // steering = 0.0 deg
    };
    auto r3 = translator.translate(frame297);
    ASSERT_TRUE(r3.has_value());
    EXPECT_NEAR(r3->getSteeringAngleDeg().value(), 0.0, 0.1);
    // All previous values should persist
    EXPECT_EQ(r3->getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(r3->getThrottlePercent().value(), 50.0, 0.1);

    // Frame 4: CAN 872 with speed = 75.0 km/h
    // DI_analogSpeed: startBit=16, 12-bit, Intel, scale=0.1, offset=0
    // 75.0 / 0.1 = 750 = 0x02EE
    std::vector<std::uint8_t> frame872 = {
        0x68, 0x03,  // CAN ID 872
        0x00, 0x00, 0xEE, 0x02, 0x00, 0x00, 0x00, 0x00  // speed = 75.0 km/h
    };
    auto r4 = translator.translate(frame872);
    ASSERT_TRUE(r4.has_value());
    EXPECT_NEAR(r4->getSpeedKmh().value(), 75.0, 0.1);

    // Final verification: all signals present from frame sequence
    EXPECT_EQ(r4->getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(r4->getThrottlePercent().value(), 50.0, 0.1);
    EXPECT_NEAR(r4->getSteeringAngleDeg().value(), 0.0, 0.1);
    EXPECT_NEAR(r4->getSpeedKmh().value(), 75.0, 0.1);
}

TEST_F(FrameReplayTest, ReplaySyntheticCanFrames_DriveSequence) {
    // Test a realistic driving sequence: Park -> Drive -> accelerate
    auto parseResult = parser.parseFile("resources/dbc/Model3CAN.dbc");
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // State 1: Park (gear=1)
    std::vector<std::uint8_t> framePark = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x10,  // gear=1 (Park)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    auto r1 = translator.translate(framePark);
    ASSERT_TRUE(r1.has_value());
    EXPECT_TRUE(r1->getGearSelector().has_value());

    // State 2: Drive (gear=4) with 0% throttle
    std::vector<std::uint8_t> frameDrive = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x40,  // gear=4 (Drive)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    auto r2 = translator.translate(frameDrive);
    ASSERT_TRUE(r2.has_value());
    EXPECT_TRUE(r2->getGearSelector().has_value());
    EXPECT_FALSE(r2->getThrottlePercent().has_value());  // No accel pedal pos in this frame

    // State 3: Accelerating (gear=4, throttle=30%, speed increasing)
    std::vector<std::uint8_t> frameAccel = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x40,  // gear=4 (Drive)
        0x4B, 0x00,  // throttle=30% (30/0.4=75=0x4B)
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    auto r3 = translator.translate(frameAccel);
    ASSERT_TRUE(r3.has_value());
    EXPECT_TRUE(r3->getGearSelector().has_value());
    EXPECT_NEAR(r3->getThrottlePercent().value(), 30.0, 0.1);

    // State 4: Speed now 20 km/h
    std::vector<std::uint8_t> frameSpeed = {
        0x68, 0x03,  // CAN ID 872
        0x00, 0x00, 0xC8, 0x00,  // speed=20.0 km/h (20/0.1=200=0xC8)
        0x00, 0x00, 0x00, 0x00
    };
    auto r4 = translator.translate(frameSpeed);
    ASSERT_TRUE(r4.has_value());
    EXPECT_NEAR(r4->getSpeedKmh().value(), 20.0, 0.1);
    // Previous state should persist
    EXPECT_NEAR(r4->getThrottlePercent().value(), 30.0, 0.1);
}