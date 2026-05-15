#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "vehicle-sim/domain/TeslaCANTranslator.h"

using namespace vehicle_sim::domain;

// ================================================
// TeslaCANTranslator Unit Tests
// DBC-aware CAN frame decoding for Tesla Model 3/Y
//
// Signal definitions from verified DBC file:
//   joshwardell/model3dbc (Tesla Model 3/Y)
//
// Key shared signals with Audi MLB Evo (same CAN IDs):
//   CAN 280 (0x118) DI_systemStatus:
//     DI_accelPedalPos, bit 32, 8-bit unsigned, scale 0.4, unit %
//     DI_brakePedalState, bit 17, 2-bit
//     DI_gear, bit 21, 3-bit
//     DI_systemState, bit 16, 1-bit
//
//   CAN 297 (0x129) SCCM_steeringAngleSensor:
//     SCCM_steeringAngle, bit 16, 14-bit unsigned, scale 0.1, offset -819.2, unit deg
//
// NOTE: Tesla-specific CAN IDs (e.g., UI_gpsVehicleSpeed CAN 985,
// RCM_inertial2 CAN 273) require exact bit position verification
// against the model3dbc DBC file before inclusion.
// ================================================

class TeslaCANTranslatorTest : public ::testing::Test {
protected:
    TeslaCANTranslator translator;
    std::vector<uint8_t> canFrame{0, 0, 0, 0, 0, 0, 0, 0};

    std::vector<uint8_t> frameWithId(uint16_t canId, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>(canId & 0xFF));
        frame.push_back(static_cast<uint8_t>((canId >> 8) & 0xFF));
        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }
};

// ================================================
// CAN 280 (0x118) — DI_systemStatus
// DI_accelPedalPos: start bit 32, 8-bit unsigned, scale 0.4, unit %
// ================================================

TEST_F(TeslaCANTranslatorTest, DecodesAcceleratorPedalFromCAN280) {
    // 75% throttle: raw = 75 / 0.4 = 187.5 -> 188 (0xBC)
    canFrame[4] = 0xBC;

    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 75.0, 0.5);
}

TEST_F(TeslaCANTranslatorTest, DecodesAcceleratorPedalAtZero) {
    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getThrottlePercent().value(), 0.0);
}

TEST_F(TeslaCANTranslatorTest, DecodesAcceleratorPedalAtFull) {
    // 100% throttle: raw = 100 / 0.4 = 250
    canFrame[4] = 250;

    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 100.0, 0.5);
}

// ================================================
// CAN 280 (0x118) — DI_systemStatus
// DI_brakePedalState: start bit 17, 2-bit
// ================================================

TEST_F(TeslaCANTranslatorTest, DecodesBrakePedalNotPressed) {
    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getBrakePercent().value(), 0.0);
}

TEST_F(TeslaCANTranslatorTest, DecodesBrakePedalPressed) {
    // DI_brakePedalState = 1 at bit 17
    // bit 17 = byte 2 bit 1 → byte 2 = 0x02
    canFrame[2] = 0x02;

    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->getBrakePercent().value(), 0.0);
}

// ================================================
// CAN 297 (0x129) — SCCM Steering Angle
// SCCM_steeringAngle: start bit 16, 14-bit unsigned, scale 0.1, offset -819.2, unit deg
// ================================================

TEST_F(TeslaCANTranslatorTest, DecodesSteeringAngleFromCAN297AtCenter) {
    // 0 deg: raw = 8192, byte 2 = 0x00, byte 3 = 0x20
    canFrame[2] = 0x00;
    canFrame[3] = 0x20;

    auto frame = frameWithId(297, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 0.0, 0.5);
}

TEST_F(TeslaCANTranslatorTest, DecodesSteeringAngleFromCAN297Right) {
    // 90 deg: raw = 9092, byte 2 = 0x84, byte 3 = 0x23
    canFrame[2] = 0x84;
    canFrame[3] = 0x23;

    auto frame = frameWithId(297, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 90.0, 0.1);
}

TEST_F(TeslaCANTranslatorTest, DecodesSteeringAngleFromCAN297Left) {
    // -90 deg: raw = 7292, byte 2 = 0x7C, byte 3 = 0x1C
    canFrame[2] = 0x7C;
    canFrame[3] = 0x1C;

    auto frame = frameWithId(297, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), -90.0, 0.1);
}

// ================================================
// Unknown CAN ID handling
// ================================================

TEST_F(TeslaCANTranslatorTest, ReturnsNulloptForUnknownCANId) {
    auto frame = frameWithId(999, canFrame);
    auto result = translator.translate(frame);

    EXPECT_FALSE(result.has_value());
}

TEST_F(TeslaCANTranslatorTest, ReturnsNulloptForTooShortFrame) {
    std::vector<uint8_t> shortFrame{0x00, 0x01};
    auto result = translator.translate(shortFrame);

    EXPECT_FALSE(result.has_value());
}

// ================================================
// State accumulation across frames
// ================================================

TEST_F(TeslaCANTranslatorTest, AccumulatesThrottleAndSteeringAcrossFrames) {
    // Frame 1: throttle 50% (CAN 280)
    canFrame[4] = 125;  // 50% / 0.4 = 125
    auto r1 = translator.translate(frameWithId(280, canFrame));

    // Frame 2: steering 45 deg (CAN 297)
    std::vector<uint8_t> steerFrame{0, 0, 0, 0, 0, 0, 0, 0};
    // 45 deg: raw = (45 + 819.2) / 0.1 = 8642
    // 8642 = compute byte encoding
    steerFrame[2] = 0xC2;
    steerFrame[3] = 0x21;
    auto r2 = translator.translate(frameWithId(297, steerFrame));

    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getThrottlePercent().value(), 50.0, 0.5);
    EXPECT_NEAR(r2->getSteeringAngleDeg().value(), 45.0, 1.0);
}

// ================================================
// isValidPacket
// ================================================

TEST_F(TeslaCANTranslatorTest, ValidatesCANFrameWithKnownId) {
    auto frame = frameWithId(280, canFrame);
    EXPECT_TRUE(translator.isValidPacket(frame));
}

TEST_F(TeslaCANTranslatorTest, RejectsTooShortFrame) {
    std::vector<uint8_t> shortFrame{0x00, 0x01, 0x00};
    EXPECT_FALSE(translator.isValidPacket(shortFrame));
}
