#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "vehicle-sim/domain/AudiMLBTranslator.h"

using namespace vehicle_sim::domain;

// ================================================
// AudiMLBTranslator Unit Tests
// DBC-aware CAN frame decoding for Audi e-tron 2021
// All signal definitions from verified DBC file:
//   commaai/opendbc vw_mlb.dbc (MLB Evo platform)
//
// CAN ID reference (vw_mlb.dbc):
//   256 (0x100) = ESP_01   — vehicle speed
//   257 (0x101) = ESP_02   — longitudinal/lateral acceleration
//   259 (0x103) = ESP_03   — wheel speeds (4 corners)
//   262 (0x106) = ESP_05   — brake pressure
//   280 (0x118) = DI_systemStatus — accelerator, brake, gear
//   297 (0x129) = SCCM_steeringAngleSensor — steering angle
// ================================================

class AudiMLBTranslatorTest : public ::testing::Test {
protected:
    AudiMLBTranslator translator;

    // Build an 8-byte CAN data payload (zeroed)
    std::vector<uint8_t> canFrame{0, 0, 0, 0, 0, 0, 0, 0};

    // Helper: prepend a CAN ID (2 bytes LE) to data for translate()
    std::vector<uint8_t> frameWithId(uint16_t canId, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>(canId & 0xFF));
        frame.push_back(static_cast<uint8_t>((canId >> 8) & 0xFF));
        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }
};

// ================================================
// CAN 256 (0x100) — ESP_01 Vehicle Speed
// Signal: ESP_v_Signal, start bit 32, 16-bit unsigned, scale 0.01, unit km/h
// ================================================

TEST_F(AudiMLBTranslatorTest, DecodesSpeedFromCAN256) {
    // 80.0 km/h: raw = 8000 = 0x1F40
    // Byte 4 = 0x40, Byte 5 = 0x1F (little-endian bit extraction)
    canFrame[4] = 0x40;
    canFrame[5] = 0x1F;

    auto frame = frameWithId(256, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSpeedKmh().value(), 80.0, 0.1);
}

TEST_F(AudiMLBTranslatorTest, DecodesSpeedFromCAN256AtZero) {
    // 0 km/h: all zeros
    auto frame = frameWithId(256, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getSpeedKmh().value(), 0.0);
}

TEST_F(AudiMLBTranslatorTest, DecodesSpeedFromCAN256AtHighwaySpeed) {
    // 130.0 km/h: raw = 13000 = 0x32C8
    canFrame[4] = 0xC8;
    canFrame[5] = 0x32;

    auto frame = frameWithId(256, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSpeedKmh().value(), 130.0, 0.1);
}

// ================================================
// CAN 257 (0x101) — ESP_02 Acceleration
// Signal: ESP_Laengsbeschl, start bit 24, 10-bit unsigned, scale 0.03125, offset -16, unit m/s^2
// Signal: ESP_Querbeschleunigung, start bit 48, 8-bit unsigned, scale 0.01, offset -1.27, unit g
// ================================================

TEST_F(AudiMLBTranslatorTest, DecodesLongitudinalAccelFromCAN257) {
    // 2.0 m/s^2: raw = (2.0 + 16) / 0.03125 = 576
    // 576 = 0b10_0100_0000
    canFrame[3] = 0x40;  // bits 0-7 of 576
    canFrame[4] = 0x02;  // bits 8-9 of 576

    auto frame = frameWithId(257, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    // Acceleration stored in g: 2.0 m/s^2 / 9.81 ≈ 0.204 g
    EXPECT_NEAR(result->getAccelerationG().value(), 0.204, 0.01);
}

TEST_F(AudiMLBTranslatorTest, DecodesLongitudinalAccelDeceleration) {
    // -5.0 m/s^2 (braking): raw = (-5.0 + 16) / 0.03125 = 352
    canFrame[3] = 0x60;
    canFrame[4] = 0x01;

    auto frame = frameWithId(257, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    // -5.0 m/s^2 / 9.81 ≈ -0.510 g
    EXPECT_NEAR(result->getAccelerationG().value(), -0.510, 0.01);
}

// ================================================
// CAN 262 (0x106) — ESP_05 Brake Pressure
// Signal: ESP_Bremsdruck, start bit 16, 10-bit unsigned, scale 0.3, offset -30, unit bar
// ================================================

TEST_F(AudiMLBTranslatorTest, DecodesBrakePressureFromCAN262) {
    // ~50 bar: raw = (50 + 30) / 0.3 ≈ 267
    // 267 = 0b01_0000_1011
    canFrame[2] = 0x0B;  // bits 0-7
    canFrame[3] = 0x01;  // bits 8-9

    auto frame = frameWithId(262, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    // Brake pressure > 30 bar means brake is pressed
    EXPECT_GT(result->getBrakePercent().value(), 0.0);
}

TEST_F(AudiMLBTranslatorTest, DecodesBrakePressureAtRest) {
    // 0 bar: raw = (0 + 30) / 0.3 = 100
    // 100 = 0b01_100100
    canFrame[2] = 0x64;
    canFrame[3] = 0x00;

    auto frame = frameWithId(262, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    // At 0 bar brake pressure, brake should be 0%
    EXPECT_DOUBLE_EQ(result->getBrakePercent().value(), 0.0);
}

// ================================================
// CAN 280 (0x118) — DI_systemStatus
// Signal: DI_accelPedalPos, start bit 32, 8-bit unsigned, scale 0.4, unit %
// Signal: DI_brakePedalState, start bit 17, 2-bit
// ================================================

TEST_F(AudiMLBTranslatorTest, DecodesAcceleratorPedalFromCAN280) {
    // 60% throttle: raw = 60 / 0.4 = 150
    canFrame[4] = 150;

    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 60.0, 0.5);
}

TEST_F(AudiMLBTranslatorTest, DecodesAcceleratorPedalAtZero) {
    // 0% throttle
    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getThrottlePercent().value(), 0.0);
}

TEST_F(AudiMLBTranslatorTest, DecodesAcceleratorPedalAtFull) {
    // 100% throttle: raw = 100 / 0.4 = 250
    canFrame[4] = 250;

    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 100.0, 0.5);
}

TEST_F(AudiMLBTranslatorTest, DecodesBrakePedalStateFromCAN280) {
    // Brake pedal pressed: DI_brakePedalState bit 17, value 1
    // Bit 17 = byte 2 bit 1 → byte 2 = 0x02
    canFrame[2] = 0x02;

    auto frame = frameWithId(280, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->getBrakePercent().value(), 0.0);
}

// ================================================
// CAN 297 (0x129) — SCCM Steering Angle
// Signal: SCCM_steeringAngle, start bit 16, 14-bit unsigned, scale 0.1, offset -819.2, unit deg
// ================================================

TEST_F(AudiMLBTranslatorTest, DecodesSteeringAngleFromCAN297AtCenter) {
    // 0 deg: raw = (0 + 819.2) / 0.1 = 8192
    // 8192 → byte 2 = 0x00, byte 3 = 0x20
    canFrame[2] = 0x00;
    canFrame[3] = 0x20;

    auto frame = frameWithId(297, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 0.0, 0.5);
}

TEST_F(AudiMLBTranslatorTest, DecodesSteeringAngleFromCAN297Positive) {
    // 180 deg right: raw = (180 + 819.2) / 0.1 = 9992
    // byte 2 = 0x08, byte 3 = 0x27
    canFrame[2] = 0x08;
    canFrame[3] = 0x27;

    auto frame = frameWithId(297, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 180.0, 1.0);
}

TEST_F(AudiMLBTranslatorTest, DecodesSteeringAngleFromCAN297Negative) {
    // -180 deg left: raw = (-180 + 819.2) / 0.1 = 6392
    // 6392 = 0b01_1000_1111_1000
    // byte 2 = 0xF8, byte 3 = 0x18
    canFrame[2] = 0xF8;
    canFrame[3] = 0x18;

    auto frame = frameWithId(297, canFrame);
    auto result = translator.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), -180.0, 1.0);
}

// ================================================
// Unknown CAN ID handling
// ================================================

TEST_F(AudiMLBTranslatorTest, ReturnsNulloptForUnknownCANId) {
    auto frame = frameWithId(999, canFrame);
    auto result = translator.translate(frame);

    EXPECT_FALSE(result.has_value());
}

TEST_F(AudiMLBTranslatorTest, ReturnsNulloptForTooShortFrame) {
    std::vector<uint8_t> shortFrame{0x00, 0x01};
    auto result = translator.translate(shortFrame);

    EXPECT_FALSE(result.has_value());
}

// ================================================
// State accumulation (translator holds last-known values)
// ================================================

TEST_F(AudiMLBTranslatorTest, AccumulatesSpeedAndThrottleAcrossFrames) {
    // Frame 1: speed 100 km/h (CAN 256)
    canFrame[4] = 0x10;
    canFrame[5] = 0x27;
    auto r1 = translator.translate(frameWithId(256, canFrame));

    // Frame 2: throttle 50% (CAN 280)
    std::vector<uint8_t> accelFrame{0, 0, 0, 0, 0, 0, 0, 0};
    accelFrame[4] = 125;  // 50% / 0.4 = 125
    auto r2 = translator.translate(frameWithId(280, accelFrame));

    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getSpeedKmh().value(), 100.0, 0.1);
    EXPECT_NEAR(r2->getThrottlePercent().value(), 50.0, 0.5);
}

// ================================================
// isValidPacket
// ================================================

TEST_F(AudiMLBTranslatorTest, ValidatesCANFrameWithKnownId) {
    auto frame = frameWithId(256, canFrame);
    EXPECT_TRUE(translator.isValidPacket(frame));
}

TEST_F(AudiMLBTranslatorTest, RejectsTooShortFrame) {
    std::vector<uint8_t> shortFrame{0x00, 0x01, 0x00};
    EXPECT_FALSE(translator.isValidPacket(shortFrame));
}
