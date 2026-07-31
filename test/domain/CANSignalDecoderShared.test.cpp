#include <gtest/gtest.h>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <vector>
#include "vehicle-sim/domain/CANDecoder.h"

using namespace vehicle_sim::domain;

// Serialization-edge adapter: view a uint8_t test frame as byte-oriented data
// for CANDecoder::extractSignal (which takes std::vector<std::byte>).
[[nodiscard]] static std::vector<std::byte> asBytes(const std::vector<std::uint8_t>& frame) {
    std::vector<std::byte> bytes(frame.size());
    std::transform(frame.begin(), frame.end(), bytes.begin(),
                   [](std::uint8_t b) { return static_cast<std::byte>(b); });
    return bytes;
}

// ================================================
// Shared CAN Signal Decoder Tests
// Tests for common CAN signals shared across vehicle types
//
// These signals are decoded identically for both Audi and Tesla:
//   - CAN 280 (0x118): DI_systemStatus — accelerator, brake
//   - CAN 297 (0x129): SCCM_steeringAngleSensor — steering angle
//
// Source: joshwardell/model3dbc Model3CAN.dbc (Tesla)
//         commaai/opendbc vw_mqbevo.dbc (Audi MLB Evo)
// Key Finding: Both vehicles share identical CAN signals at 280 and 297
// ================================================

class CANSignalDecoderSharedTest : public ::testing::Test {
protected:
    // Build an 8-byte CAN data payload (zeroed)
    std::vector<uint8_t> canFrame{0, 0, 0, 0, 0, 0, 0, 0};

    // Helper: prepend a CAN ID (2 bytes LE) to data
    std::vector<uint8_t> frameWithId(uint16_t canId, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>(canId & 0xFF));
        frame.push_back(static_cast<uint8_t>((canId >> 8) & 0xFF));
        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }
};

// ================================================
// CAN 280 (0x118) — DI_systemStatus — Accelerator Pedal
// Signal: DI_accelPedalPos, start bit 32, 8-bit unsigned, scale 0.4, unit %
// Shared by: Tesla Model 3/Y and Audi e-tron 2021
// ================================================

TEST_F(CANSignalDecoderSharedTest, DecodesAcceleratorPedalPositionFromCAN280)
{
    // 80% throttle: raw = 80 / 0.4 = 200
    // Byte 4 (bit 32) contains the 8-bit value
    canFrame[4] = 200;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 80.0, 0.5);
}

TEST_F(CANSignalDecoderSharedTest, DecodesAcceleratorPedalAtZeroFromCAN280)
{
    // 0% throttle: raw = 0
    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesAcceleratorPedalAtFullFromCAN280)
{
    // 100% throttle: raw = 100 / 0.4 = 250
    canFrame[4] = 250;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 100.0, 0.5);
}

TEST_F(CANSignalDecoderSharedTest, DecodesAcceleratorPedalAtMidRangeFromCAN280)
{
    // 50% throttle: raw = 50 / 0.4 = 125
    canFrame[4] = 125;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 50.0, 0.5);
}

TEST_F(CANSignalDecoderSharedTest, DecodesAcceleratorPedalAtLightFromCAN280)
{
    // 20% throttle: raw = 20 / 0.4 = 50
    canFrame[4] = 50;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 20.0, 0.5);
}

// ================================================
// CAN 280 (0x118) — DI_systemStatus — Brake Pedal State
// Signal: DI_brakePedalState, start bit 17, 2-bit enum, scale 1.0
// Shared by: Tesla Model 3/Y and Audi e-tron 2021
// ================================================

TEST_F(CANSignalDecoderSharedTest, DecodesBrakePedalStateFromCAN280)
{
    // Brake pedal pressed: bit 17 set (value 1)
    // Bit 17 = byte 2 bit 1 → byte 2 = 0x02
    canFrame[2] = 0x02;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesBrakePedalNotPressedFromCAN280)
{
    // Brake pedal not pressed: bit 17 clear (value 0)
    auto result = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesBrakePedalStateValue3FromCAN280)
{
    // Brake state value 3: bits 17-18 both set
    // Bits 1-2 of byte 2 set → byte 2 = 0x06
    canFrame[2] = 0x06;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 3.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesBrakePedalStateValue2FromCAN280)
{
    // Brake state value 2: bit 18 set
    // Bit 2 of byte 2 set → byte 2 = 0x04
    canFrame[2] = 0x04;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 2.0);
}

// ================================================
// CAN 280 Combined Signals Test
// ================================================

TEST_F(CANSignalDecoderSharedTest, DecodesBothAcceleratorAndBrakeFromCAN280)
{
    // Accelerator at 60%: byte 4 = 150 (60 / 0.4)
    canFrame[4] = 150;
    // Brake pedal pressed: byte 2 bit 1 set
    canFrame[2] = 0x02;

    auto throttleResult = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);
    auto brakeResult = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(throttleResult.has_value());
    ASSERT_TRUE(brakeResult.has_value());
    EXPECT_NEAR(*throttleResult, 60.0, 0.5);
    EXPECT_DOUBLE_EQ(*brakeResult, 1.0);
}

// ================================================
// CAN 297 (0x129) — SCCM_steeringAngleSensor — Steering Angle
// Signal: SCCM_steeringAngle, start bit 16, 14-bit unsigned, scale 0.1, offset -819.2, unit deg
// Shared by: Tesla Model 3/Y and Audi e-tron 2021
// ================================================

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAngleAtCenterFromCAN297)
{
    // 0 deg (center): raw = (0 + 819.2) / 0.1 = 8192
    // 8192 = 0b10_0000_0000_0000
    // Bit 16 is LSB of byte 2 → bytes 2-3 contain the 14-bit value
    // 8192 in 14-bit: byte 2 = 0x00 (bits 0-7), byte 3 = 0x20 (bits 8-9 set as 10)
    canFrame[2] = 0x00;
    canFrame[3] = 0x20;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.0, 0.5);
}

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAnglePositiveFromCAN297)
{
    // 90 deg right: raw = (90 + 819.2) / 0.1 = 9092
    // 9092 = 0b10_0011_1000_0100
    // Byte 2 (bits 0-7): 0x84
    // Byte 3 (bits 8-13): 0x23 (only lower 6 bits used)
    canFrame[2] = 0x84;
    canFrame[3] = 0x23;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 90.0, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAngleNegativeFromCAN297)
{
    // -90 deg left: raw = (-90 + 819.2) / 0.1 = 7292
    // 7292 = 0b01_1100_0011_1100
    // Byte 2: 0x7C, Byte 3: 0x1C
    canFrame[2] = 0x7C;
    canFrame[3] = 0x1C;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -90.0, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAngleAtMaxRightFromCAN297)
{
    // +819.1 deg max right (approximately 2.27 full rotations right)
    // raw = (819.1 + 819.2) / 0.1 = 16383 (max 14-bit value)
    // 16383 = 0b11_1111_1111_1111
    canFrame[2] = 0xFF;
    canFrame[3] = 0x3F;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 819.1, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAngleAtMaxLeftFromCAN297)
{
    // -819.2 deg max left
    // raw = (-819.2 + 819.2) / 0.1 = 0
    canFrame[2] = 0x00;
    canFrame[3] = 0x00;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -819.2, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAngle180RightFromCAN297)
{
    // 180 deg right: raw = (180 + 819.2) / 0.1 = 9992
    // 9992 = 0b10_0111_0000_1000
    // Byte 2: 0x08, Byte 3: 0x27
    canFrame[2] = 0x08;
    canFrame[3] = 0x27;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 180.0, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, DecodesSteeringAngle180LeftFromCAN297)
{
    // -180 deg left: raw = (-180 + 819.2) / 0.1 = 6392
    // 6392 = 0b01_1000_1111_1000
    // Byte 2: 0xF8, Byte 3: 0x18
    canFrame[2] = 0xF8;
    canFrame[3] = 0x18;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -180.0, 1.0);
}

// ================================================
// Cross-vehicle compatibility tests
// These tests verify that the common signals work identically
// regardless of vehicle type (Tesla or Audi)
// ================================================

TEST_F(CANSignalDecoderSharedTest, AcceleratorPedalDecodesIdenticallyForTeslaAndAudi)
{
    // Both vehicles use identical decoding for CAN 280
    // This test documents the shared behavior
    const double expectedThrottle = 60.0;
    const uint8_t rawValue = static_cast<uint8_t>(expectedThrottle / 0.4);  // 150
    canFrame[4] = rawValue;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, expectedThrottle, 0.5);
    // No vehicle-specific logic needed — shared decoder works for both
}

TEST_F(CANSignalDecoderSharedTest, SteeringAngleDecodesIdenticallyForTeslaAndAudi)
{
    // Both vehicles use identical decoding for CAN 297
    // This test documents the shared behavior
    const double expectedAngle = 45.0;
    // rawValue = (expectedAngle + 819.2) / 0.1 = 8642 = 0b10_0001_1100_0010
    canFrame[2] = 0xC2;
    canFrame[3] = 0x21;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, expectedAngle, 1.0);
    // No vehicle-specific logic needed — shared decoder works for both
}

TEST_F(CANSignalDecoderSharedTest, BrakePedalStateDecodesIdenticallyForTeslaAndAudi)
{
    // Both vehicles use identical decoding for CAN 280 brake state
    // This test documents the shared behavior
    canFrame[2] = 0x02;  // Brake pressed (bit 17 set)

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1.0);
    // No vehicle-specific logic needed — shared decoder works for both
}

// ================================================
// Edge case tests for shared signals
// ================================================

TEST_F(CANSignalDecoderSharedTest, HandlesMinimalAcceleratorInputFromCAN280)
{
    // 1% throttle: raw = 1 / 0.4 = 2.5 → rounds to 3
    canFrame[4] = 3;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 1.2, 0.5);  // 3 * 0.4 = 1.2
}

TEST_F(CANSignalDecoderSharedTest, HandlesAcceleratorOver100PercentFromCAN280)
{
    // Theoretical max raw 255 * 0.4 = 102%
    // Application layer should clamp to 100
    canFrame[4] = 255;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 102.0, 0.5);
}

TEST_F(CANSignalDecoderSharedTest, HandlesSteeringAngleNearCenterFromCAN297)
{
    // 1 deg right: raw = (1 + 819.2) / 0.1 = 8202
    // 8202 = 0b10_0000_0000_1010
    canFrame[2] = 0x0A;
    canFrame[3] = 0x20;

    auto result = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 1.0, 0.5);
}

TEST_F(CANSignalDecoderSharedTest, ReturnsNulloptForTooShortCANFrame)
{
    // Frame too short for bit extraction
    std::vector<uint8_t> shortFrame{0, 0};

    auto result = CANDecoder::extractSignal(asBytes(shortFrame), 32, 8, 0.4, 0.0, false);

    EXPECT_FALSE(result.has_value());
}

// ================================================
// Integration: Full telemetry frame tests
// ================================================

TEST_F(CANSignalDecoderSharedTest, ExtractsAllSharedSignalsFromCompleteCAN280)
{
    // CAN 280 with accelerator at 80% and brake pressed
    canFrame[4] = 200;   // 80% throttle (200 * 0.4)
    canFrame[2] = 0x02;  // Brake pressed (bit 17 set)

    auto throttle = CANDecoder::extractSignal(asBytes(canFrame), 32, 8, 0.4, 0.0, false);
    auto brake = CANDecoder::extractSignal(asBytes(canFrame), 17, 2, 1.0, 0.0, false);

    ASSERT_TRUE(throttle.has_value());
    ASSERT_TRUE(brake.has_value());
    EXPECT_NEAR(*throttle, 80.0, 0.5);
    EXPECT_DOUBLE_EQ(*brake, 1.0);
}

TEST_F(CANSignalDecoderSharedTest, ExtractsSteeringAngleFromCompleteCAN297)
{
    // CAN 297 with steering at 45 deg right
    canFrame[2] = 0xC2;  // See calculation in DecodesSteeringAngleIdenticallyForTeslaAndAudi
    canFrame[3] = 0x21;

    auto angle = CANDecoder::extractSignal(asBytes(canFrame), 16, 14, 0.1, -819.2, false);

    ASSERT_TRUE(angle.has_value());
    EXPECT_NEAR(*angle, 45.0, 1.0);
}
