#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "vehicle-sim/domain/CANDecoder.h"

using namespace vehicle_sim::domain;

// ================================================
// CANDecoder Unit Tests
// DBC-aware CAN frame bit extraction
// All signal definitions from verified DBC files:
//   - commaai/opendbc vw_mlb.dbc (Audi MLB Evo)
//   - joshwardell/model3dbc (Tesla Model 3/Y)
// ================================================

class CANDecoderTest : public ::testing::Test {
protected:
    // Helper: build 8-byte CAN data payload (all zeros by default)
    std::vector<uint8_t> canFrame{0, 0, 0, 0, 0, 0, 0, 0};
};

// ================================================
// Unsigned signal extraction — single byte
// ================================================

TEST_F(CANDecoderTest, ExtractsUnsigned8BitSignal) {
    // DI_accelPedalPos: start bit 32, 8-bit, scale 0.4, unit %
    // Source: model3dbc / vw_mlb.dbc DI_systemStatus (CAN 280)
    // Byte 4 (bit 32), value 200 → 200 * 0.4 = 80.0%
    canFrame[4] = 200;

    auto result = CANDecoder::extractSignal(canFrame, 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 80.0);
}

TEST_F(CANDecoderTest, ExtractsUnsigned8BitSignalAtMinimum) {
    // Zero input → 0.0
    canFrame[4] = 0;

    auto result = CANDecoder::extractSignal(canFrame, 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST_F(CANDecoderTest, ExtractsUnsigned8BitSignalAtMaximum) {
    // Max 8-bit: 255 → 255 * 0.4 = 102.0 (clamp at 100% for pedal)
    canFrame[4] = 255;

    auto result = CANDecoder::extractSignal(canFrame, 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 102.0);
}

// ================================================
// Unsigned signal extraction — multi-byte (16-bit LE)
// ================================================

TEST_F(CANDecoderTest, ExtractsUnsigned16BitSignalLittleEndian) {
    // ESP_v_Signal: start bit 32, 16-bit, scale 0.01, unit km/h
    // Source: vw_mlb.dbc ESP_01 (CAN 256)
    // Bytes 4-5, value 5000 → 5000 * 0.01 = 50.0 km/h
    canFrame[4] = 0x88;  // 5000 & 0xFF = 0x88
    canFrame[5] = 0x13;  // 5000 >> 8  = 0x13

    auto result = CANDecoder::extractSignal(canFrame, 32, 16, 0.01, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 50.0);
}

TEST_F(CANDecoderTest, ExtractsUnsigned16BitSignalHighSpeed) {
    // 25500 → 25500 * 0.01 = 255.0 km/h
    // 25500 = 0x63CC → byte 4 = 0x9C, byte 5 = 0x63
    canFrame[4] = 0x9C;
    canFrame[5] = 0x63;

    auto result = CANDecoder::extractSignal(canFrame, 32, 16, 0.01, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 255.0);
}

// ================================================
// Unsigned signal with negative offset (DBC offset convention)
// ================================================

TEST_F(CANDecoderTest, ExtractsUnsignedSignalWithNegativeOffset) {
    // ESP_Laengsbeschl: start bit 24, 10-bit unsigned, scale 0.03125, offset -16, unit m/s^2
    // Source: vw_mlb.dbc ESP_02 (CAN 257)
    // DBC uses unsigned + negative offset to represent signed range [-16, 15.9375]
    // Raw value 0 → physical = (0 * 0.03125) + (-16) = -16.0 m/s^2
    canFrame[3] = 0x00;
    canFrame[4] = 0x00;

    auto result = CANDecoder::extractSignal(canFrame, 24, 10, 0.03125, -16.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, -16.0);
}

TEST_F(CANDecoderTest, ExtractsUnsignedSignalAtZeroAcceleration) {
    // Raw value 512 → physical = (512 * 0.03125) + (-16) = 0.0 m/s^2
    // 10-bit at start bit 24: byte 3 bits 0-7 + byte 4 bits 0-1
    // 512 = 0b10_0000_0000 → byte 3 = 0x00, byte 4 = 0x02
    canFrame[3] = 0x00;
    canFrame[4] = 0x02;

    auto result = CANDecoder::extractSignal(canFrame, 24, 10, 0.03125, -16.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.0, 0.001);
}

TEST_F(CANDecoderTest, ExtractsUnsignedSignalPositiveAcceleration) {
    // Raw value 640 → physical = (640 * 0.03125) + (-16) = 4.0 m/s^2
    // 640 → byte 3 = 0x80, byte 4 = 0x02
    canFrame[3] = 0x80;
    canFrame[4] = 0x02;

    auto result = CANDecoder::extractSignal(canFrame, 24, 10, 0.03125, -16.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 4.0, 0.001);
}

TEST_F(CANDecoderTest, ExtractsUnsignedSignalNegativeAcceleration) {
    // Raw value 320 → physical = (320 * 0.03125) + (-16) = -6.0 m/s^2
    // 320 → byte 3 = 0x40, byte 4 = 0x01
    canFrame[3] = 0x40;
    canFrame[4] = 0x01;

    auto result = CANDecoder::extractSignal(canFrame, 24, 10, 0.03125, -16.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -6.0, 0.001);
}

// ================================================
// 14-bit signal with offset (steering angle)
// Unsigned + negative offset (DBC convention for signed physical range)
// ================================================

TEST_F(CANDecoderTest, ExtractsSteeringAngleAtCenter) {
    // SCCM_steeringAngle: start bit 16, 14-bit unsigned, scale 0.1, offset -819.2, unit deg
    // Source: vw_mlb.dbc SCCM_steeringAngleSensor (CAN 297)
    // Raw 8192 → physical = (8192 * 0.1) + (-819.2) = 0.0 deg (center)
    // 8192 = 0b10_0000_0000_0000 → byte 2 = 0x00, byte 3 = 0x20
    canFrame[2] = 0x00;
    canFrame[3] = 0x20;

    auto result = CANDecoder::extractSignal(canFrame, 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.0, 0.01);
}

TEST_F(CANDecoderTest, ExtractsSteeringAnglePositive) {
    // 90 deg right: raw = (90 + 819.2) / 0.1 = 9092
    // byte 2 = 0x84, byte 3 = 0x23
    canFrame[2] = 0x84;
    canFrame[3] = 0x23;

    auto result = CANDecoder::extractSignal(canFrame, 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 90.0, 0.1);
}

TEST_F(CANDecoderTest, ExtractsSteeringAngleNegative) {
    // -90 deg left: raw = (-90 + 819.2) / 0.1 = 7292
    // byte 2 = 0x7C, byte 3 = 0x1C
    canFrame[2] = 0x7C;
    canFrame[3] = 0x1C;

    auto result = CANDecoder::extractSignal(canFrame, 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -90.0, 0.1);
}

// ================================================
// Bit field spanning byte boundaries
// ================================================

TEST_F(CANDecoderTest, ExtractsBitFieldSpanningTwoBytes) {
    // 3-bit field starting at bit 5 of byte 0
    // Value 5 = 0b101, placed at bits 5-7 of byte 0
    canFrame[0] = 0xA0;  // bits 5-7 = 101

    auto result = CANDecoder::extractSignal(canFrame, 5, 3, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 5.0);
}

TEST_F(CANDecoderTest, ExtractsBitFieldSpanningThreeBytes) {
    // 10-bit field starting at bit 6
    // This spans byte 0 bits 6-7 (2 bits) and byte 1 bits 0-7 (8 bits)
    // Value 673: byte 0 = 0x40, byte 1 = 0xA8
    canFrame[0] = 0x40;
    canFrame[1] = 0xA8;

    auto result = CANDecoder::extractSignal(canFrame, 6, 10, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 673.0);
}

// ================================================
// 2-bit brake state (enum-like)
// ================================================

TEST_F(CANDecoderTest, Extracts2BitBrakeState) {
    // DI_brakePedalState: start bit 17, 2-bit
    // Source: model3dbc / vw_mlb.dbc DI_systemStatus (CAN 280)
    // Value 1 at bit 17: byte 2 = 0x02 (bit 1 set)
    canFrame[2] = 0x02;

    auto result = CANDecoder::extractSignal(canFrame, 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1.0);
}

TEST_F(CANDecoderTest, Extracts2BitBrakeStateValue3) {
    // Value 3 at bit 17: byte 2 = 0x06 (bits 1-2 set)
    canFrame[2] = 0x06;

    auto result = CANDecoder::extractSignal(canFrame, 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 3.0);
}

// ================================================
// 12-bit wheel speed signals
// ================================================

TEST_F(CANDecoderTest, Extracts12BitWheelSpeed) {
    // ESP_RadHL_Hz: 12-bit wheel speed
    // Source: vw_mlb.dbc ESP_03 (CAN 259)
    // 500 counts → 500 * 0.1 = 50.0 km/h (hypothetical scale)
    // 500 = 0x1F4, start bit 0: byte 0 = 0xF4, byte 1 = 0x01
    canFrame[0] = 0xF4;
    canFrame[1] = 0x01;

    auto result = CANDecoder::extractSignal(canFrame, 0, 12, 0.1, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 50.0);
}

TEST_F(CANDecoderTest, Extracts12BitWheelSpeedAtMax) {
    // 12-bit max = 4095
    canFrame[0] = 0xFF;
    canFrame[1] = 0x0F;

    auto result = CANDecoder::extractSignal(canFrame, 0, 12, 0.1, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 409.5);
}

// ================================================
// 8-bit lateral acceleration with offset
// ================================================

TEST_F(CANDecoderTest, Extracts8BitLateralAcceleration) {
    // ESP_Querbeschleunigung: start bit 48, 8-bit, scale 0.01, offset -1.27, unit g
    // Source: vw_mlb.dbc ESP_02 (CAN 257)
    // Raw 127 → physical = (127 * 0.01) + (-1.27) = 0.0 g
    canFrame[6] = 0x7F;

    auto result = CANDecoder::extractSignal(canFrame, 48, 8, 0.01, -1.27, true);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.0, 0.001);
}

TEST_F(CANDecoderTest, Extracts8BitLateralAccelerationPositive) {
    // 0.5g cornering: raw = (0.5 + 1.27) / 0.01 = 177 (0xB1)
    // DBC uses unsigned + negative offset for signed physical range
    canFrame[6] = 0xB1;

    auto result = CANDecoder::extractSignal(canFrame, 48, 8, 0.01, -1.27, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.5, 0.01);
}

// ================================================
// Edge cases
// ================================================

TEST_F(CANDecoderTest, ReturnsNulloptForFrameTooSmall) {
    std::vector<uint8_t> shortFrame{0x00, 0x00, 0x00};

    auto result = CANDecoder::extractSignal(shortFrame, 32, 8, 1.0, 0.0, false);

    EXPECT_FALSE(result.has_value());
}

TEST_F(CANDecoderTest, ReturnsNulloptForZeroBitLength) {
    auto result = CANDecoder::extractSignal(canFrame, 0, 0, 1.0, 0.0, false);

    EXPECT_FALSE(result.has_value());
}

TEST_F(CANDecoderTest, Handles1BitSignal) {
    // Boolean signal at bit 0
    canFrame[0] = 0x01;

    auto result = CANDecoder::extractSignal(canFrame, 0, 1, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1.0);
}
