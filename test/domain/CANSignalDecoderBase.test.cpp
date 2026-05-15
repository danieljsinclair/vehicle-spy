#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <memory>
#include "vehicle-sim/domain/CANSignalDecoderBase.h"
#include "vehicle-sim/domain/ITimeProvider.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// CANSignalDecoderBase Unit Tests
//
// Tests the base class shared by all CAN translators:
//   - decodeCAN280: accelerator pedal + brake state
//   - decodeCAN297: steering angle
//   - isValidFrame: CAN ID registry validation
//   - translateFrame: full pipeline decode
//   - State accumulation across multiple frames
//
// DBC sources:
//   - joshwardell/model3dbc (Tesla Model 3/Y)
//   - commaai/opendbc vw_mlb.dbc (Audi MLB Evo)
// ================================================

// Testable subclass exposes protected members for white-box testing.
// Static decode methods are wrapped in public forwarding functions
// because C++ access checking for inherited static protected members
// does not allow external callers even with `using` declarations.
class TestableDecoder : public CANSignalDecoderBase {
public:
    using CANSignalDecoderBase::CANSignalDecoderBase;

    using CANSignalDecoderBase::extractCANId;
    using CANSignalDecoderBase::buildSignal;
    using CANSignalDecoderBase::setThrottlePercent;
    using CANSignalDecoderBase::setBrakePercent;
    using CANSignalDecoderBase::setSteeringAngleDeg;
    using CANSignalDecoderBase::setSpeedKmh;

    using CANSignalDecoderBase::CAN_FRAME_SIZE;

    static std::pair<double, double> decode280(const std::vector<uint8_t>& data) {
        return decodeCAN280(data);
    }

    static double decode297(const std::vector<uint8_t>& data) {
        return decodeCAN297(data);
    }
};

// ================================================
// Test fixture with shared helpers
//
// Frame format per header:
//   [canId_lo, canId_hi, data_byte_0, ..., data_byte_7]
//   CAN ID is 16-bit little-endian in first 2 bytes.
//   CAN_DATA_OFFSET = 2, CAN_FRAME_SIZE = 10.
//
// DBC references:
//   CAN 280 (0x118): DI_accelPedalPos bit 32, 8-bit, scale 0.4, offset 0
//                    DI_brakePedalState bit 17, 2-bit, scale 1, offset 0
//   CAN 297 (0x129): SCCM_steeringAngle bit 16, 14-bit, scale 0.1, offset -819.2
// ================================================

class CANSignalDecoderBaseTest : public ::testing::Test {
protected:
    // Build a full 10-byte CAN frame: 2-byte LE CAN ID + 8 data bytes.
    static std::vector<uint8_t> makeFrame(uint16_t canId,
                                          const std::vector<uint8_t>& data) {
        std::vector<uint8_t> frame(TestableDecoder::CAN_FRAME_SIZE, 0);
        frame[0] = static_cast<uint8_t>(canId & 0xFF);
        frame[1] = static_cast<uint8_t>((canId >> 8) & 0xFF);
        for (size_t i = 0; i < data.size() && i + 2 < frame.size(); ++i) {
            frame[i + 2] = data[i];
        }
        return frame;
    }

    static constexpr uint16_t CAN_ID_280 = 0x0118;
    static constexpr uint16_t CAN_ID_297 = 0x0129;
};

// ================================================
// CAN 280 (0x118) — DI_systemStatus
// DBC: DI_accelPedalPos start bit 32, 8-bit, scale 0.4, offset 0, range 0-100%
// DBC: DI_brakePedalState start bit 19, 2-bit, scale 1, offset 0, enum 0-2
//
// Tests the real bit-extraction logic against the DBC specification:
// given known wire bytes, do we get the expected physical values out?
// ================================================

// Test 1: Zero throttle, no brake — all bytes zero
TEST_F(CANSignalDecoderBaseTest, ZeroThrottle_NoBrake)
{
    const std::vector<uint8_t> data(8, 0);

    auto [accel, brake] = TestableDecoder::decode280(data);

    EXPECT_DOUBLE_EQ(accel, 0.0);
    EXPECT_DOUBLE_EQ(brake, 0.0);
}

// Test 2: Half throttle (50%), no brake
// raw = 50 / 0.4 = 125 = 0x7D at byte 4 (bit 32)
TEST_F(CANSignalDecoderBaseTest, HalfThrottle_NoBrake)
{
    std::vector<uint8_t> data(8, 0);
    data[4] = 0x7D;

    auto [accel, brake] = TestableDecoder::decode280(data);

    EXPECT_DOUBLE_EQ(accel, 50.0);
    EXPECT_DOUBLE_EQ(brake, 0.0);
}

// Test 3: Full throttle (100%), no brake
// raw = 100 / 0.4 = 250 = 0xFA at byte 4
TEST_F(CANSignalDecoderBaseTest, FullThrottle_NoBrake)
{
    std::vector<uint8_t> data(8, 0);
    data[4] = 0xFA;

    auto [accel, brake] = TestableDecoder::decode280(data);

    EXPECT_DOUBLE_EQ(accel, 100.0);
    EXPECT_DOUBLE_EQ(brake, 0.0);
}

// Test 4: Brake pressed (enum 2), no throttle
// DI_brakePedalState start bit 19, 2-bit. Enum value 2 = bit 1 of field set.
// bit 1 of field = start bit 20 = byte 2 bit 4 = 0x10
TEST_F(CANSignalDecoderBaseTest, BrakePressed_NoThrottle)
{
    std::vector<uint8_t> data(8, 0);
    data[2] = 0x10;

    auto [accel, brake] = TestableDecoder::decode280(data);

    EXPECT_DOUBLE_EQ(accel, 0.0);
    EXPECT_DOUBLE_EQ(brake, 2.0);
}

// Test 5: Simultaneous throttle and brake
// byte 2 = 0x10 (brake enum 2 at start bit 19), byte 4 = 0x7D (50% throttle)
TEST_F(CANSignalDecoderBaseTest, ThrottleAndBrakeSimultaneous)
{
    std::vector<uint8_t> data(8, 0);
    data[2] = 0x10;
    data[4] = 0x7D;

    auto [accel, brake] = TestableDecoder::decode280(data);

    EXPECT_DOUBLE_EQ(accel, 50.0);
    EXPECT_DOUBLE_EQ(brake, 2.0);
}

// ================================================
// CAN 297 (0x129) — SCCM_steeringAngleSensor
// DBC: SCCM_steeringAngle start bit 16, 14-bit, scale 0.1, offset -819.2
// Raw in bytes 2-3: byte 2 = lower 8 bits, byte 3 = upper 6 bits.
// Physical = raw * 0.1 + (-819.2)
// ================================================

// Test 6: Center position — 0 degrees
// raw = (0 + 819.2) / 0.1 = 8192 = 0x2000
// byte 2 = 0x00, byte 3 = 0x20
TEST_F(CANSignalDecoderBaseTest, CenterPosition)
{
    std::vector<uint8_t> data(8, 0);
    data[2] = 0x00;
    data[3] = 0x20;

    auto angle = TestableDecoder::decode297(data);

    EXPECT_NEAR(angle, 0.0, 0.05);
}

// Test 7: Moderate right — +100 degrees
// raw = (100 + 819.2) / 0.1 = 9192 = 0x23E8
// byte 2 = 0xE8, byte 3 = 0x23
TEST_F(CANSignalDecoderBaseTest, ModerateRight)
{
    std::vector<uint8_t> data(8, 0);
    data[2] = 0xE8;
    data[3] = 0x23;

    auto angle = TestableDecoder::decode297(data);

    EXPECT_NEAR(angle, 100.0, 0.05);
}

// Test 8: Moderate left — -100 degrees
// raw = (-100 + 819.2) / 0.1 = 7192 = 0x1C18
// byte 2 = 0x18, byte 3 = 0x1C
TEST_F(CANSignalDecoderBaseTest, ModerateLeft)
{
    std::vector<uint8_t> data(8, 0);
    data[2] = 0x18;
    data[3] = 0x1C;

    auto angle = TestableDecoder::decode297(data);

    EXPECT_NEAR(angle, -100.0, 0.05);
}

// Test 9: Full lock — max 14-bit value
// raw = 16383 = 0x3FFF -> byte 2 = 0xFF, byte 3 = 0x3F -> 819.1 degrees
TEST_F(CANSignalDecoderBaseTest, FullLock)
{
    std::vector<uint8_t> data(8, 0);
    data[2] = 0xFF;
    data[3] = 0x3F;

    auto angle = TestableDecoder::decode297(data);

    EXPECT_NEAR(angle, 819.1, 0.1);
}

// ================================================
// Frame validation — isValidFrame
// Tests decoder registry lookup via the public API.
// ================================================

// Test 10: Frame with CAN ID in registry is accepted
TEST_F(CANSignalDecoderBaseTest, ValidFrame_Accepted)
{
    TestableDecoder decoder(
        std::make_unique<MockTimeProvider>(0),
        CANSignalDecoderBase::DecoderMap{
            {CAN_ID_280, [](const auto&) {}},
            {CAN_ID_297, [](const auto&) {}}
        });

    auto frame = makeFrame(CAN_ID_280, std::vector<uint8_t>(8, 0));

    EXPECT_TRUE(decoder.isValidFrame(frame));
}

// Test 11: Frame with unregistered CAN ID is rejected
TEST_F(CANSignalDecoderBaseTest, UnknownCANId_Rejected)
{
    TestableDecoder decoder(
        std::make_unique<MockTimeProvider>(0),
        CANSignalDecoderBase::DecoderMap{
            {CAN_ID_280, [](const auto&) {}}
        });

    // CAN ID 0x02A8 (680) is not in the decoder registry
    auto frame = makeFrame(0x02A8, std::vector<uint8_t>(8, 0));

    EXPECT_FALSE(decoder.isValidFrame(frame));
}

// Test 12: Frame shorter than 2 bytes is rejected
TEST_F(CANSignalDecoderBaseTest, TruncatedFrame_Rejected)
{
    TestableDecoder decoder(
        std::make_unique<MockTimeProvider>(0),
        CANSignalDecoderBase::DecoderMap{
            {CAN_ID_280, [](const auto&) {}}
        });

    // Single byte — too short for extractCANId (< 2 bytes -> returns 0)
    std::vector<uint8_t> shortFrame{0x18};

    EXPECT_FALSE(decoder.isValidFrame(shortFrame));
}

// ================================================
// Full pipeline — translateFrame
// End-to-end: frame -> extractCANId -> decoder lambda -> state -> VehicleSignal.
// Lambdas use real decodeCAN280/decodeCAN297 via TestableDecoder wrappers
// and set state via exposed protected setters.
// ================================================

// Test 13: Valid CAN 280 frame with real decoder lambda produces
// VehicleSignal with correct throttle and brake values.
TEST_F(CANSignalDecoderBaseTest, TranslateValid280_ReturnsSignal)
{
    // Two-phase construction: lambda needs decoder pointer for setters.
    auto decoderOwner = std::unique_ptr<TestableDecoder>{};

    CANSignalDecoderBase::DecoderMap map;
    map[CAN_ID_280] = [&decoderOwner](const std::vector<uint8_t>& data) {
        auto [throttle, brake] = TestableDecoder::decode280(data);
        decoderOwner->setThrottlePercent(throttle);
        decoderOwner->setBrakePercent(brake);
    };

    decoderOwner = std::make_unique<TestableDecoder>(
        std::make_unique<MockTimeProvider>(0),
        std::move(map));

    // 50% throttle, no brake: byte 4 = 0x7D
    std::vector<uint8_t> data(8, 0);
    data[4] = 0x7D;
    auto frame = makeFrame(CAN_ID_280, data);

    auto result = decoderOwner->translateFrame(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getThrottlePercent().value(), 50.0);
    EXPECT_DOUBLE_EQ(result->getBrakePercent().value(), 0.0);
}

// Test 14: Unregistered CAN ID returns nullopt
TEST_F(CANSignalDecoderBaseTest, TranslateUnknownFrame_ReturnsNullopt)
{
    TestableDecoder decoder(
        std::make_unique<MockTimeProvider>(0),
        CANSignalDecoderBase::DecoderMap{
            {CAN_ID_280, [](const auto&) {}}
        });

    // CAN ID 0x02A8 not in decoder map
    auto frame = makeFrame(0x02A8, std::vector<uint8_t>(8, 0));

    auto result = decoder.translateFrame(frame);

    EXPECT_FALSE(result.has_value());
}

// Test 15: Frame shorter than CAN_FRAME_SIZE (10 bytes) returns nullopt
TEST_F(CANSignalDecoderBaseTest, TranslateShortFrame_ReturnsNullopt)
{
    TestableDecoder decoder(
        std::make_unique<MockTimeProvider>(0),
        CANSignalDecoderBase::DecoderMap{
            {CAN_ID_280, [](const auto&) {}}
        });

    // 5 bytes — below CAN_FRAME_SIZE of 10
    std::vector<uint8_t> shortFrame{0x18, 0x01, 0x00, 0x00, 0x00};

    auto result = decoder.translateFrame(shortFrame);

    EXPECT_FALSE(result.has_value());
}

// ================================================
// State accumulation — the core real-time simulation scenario.
// The simulator receives CAN frames from different message IDs
// and must assemble them into a single coherent VehicleSignal.
// ================================================

// Test 16: After processing CAN 280 (throttle+brake) then CAN 297 (steering),
// the second translateFrame result contains throttle from 280 AND steering from 297.
TEST_F(CANSignalDecoderBaseTest, MultipleFrames_AccumulateCompleteSignal)
{
    auto decoderOwner = std::unique_ptr<TestableDecoder>{};

    CANSignalDecoderBase::DecoderMap map;
    map[CAN_ID_280] = [&decoderOwner](const std::vector<uint8_t>& data) {
        auto [throttle, brake] = TestableDecoder::decode280(data);
        decoderOwner->setThrottlePercent(throttle);
        decoderOwner->setBrakePercent(brake);
    };
    map[CAN_ID_297] = [&decoderOwner](const std::vector<uint8_t>& data) {
        auto angle = TestableDecoder::decode297(data);
        decoderOwner->setSteeringAngleDeg(angle);
    };

    decoderOwner = std::make_unique<TestableDecoder>(
        std::make_unique<MockTimeProvider>(0),
        std::move(map));

    // Frame 1: CAN 280 — 50% throttle, no brake
    std::vector<uint8_t> data280(8, 0);
    data280[4] = 0x7D;
    auto frame280 = makeFrame(CAN_ID_280, data280);
    auto result1 = decoderOwner->translateFrame(frame280);
    ASSERT_TRUE(result1.has_value());
    EXPECT_DOUBLE_EQ(result1->getThrottlePercent().value(), 50.0);
    EXPECT_DOUBLE_EQ(result1->getBrakePercent().value(), 0.0);
    // Steering not yet received — should be default 0
    EXPECT_DOUBLE_EQ(result1->getSteeringAngleDeg().value(), 0.0);

    // Frame 2: CAN 297 — +100 deg steering
    std::vector<uint8_t> data297(8, 0);
    data297[2] = 0xE8;
    data297[3] = 0x23;
    auto frame297 = makeFrame(CAN_ID_297, data297);
    auto result2 = decoderOwner->translateFrame(frame297);

    ASSERT_TRUE(result2.has_value());
    // Accumulated throttle from frame 1 persists
    EXPECT_DOUBLE_EQ(result2->getThrottlePercent().value(), 50.0);
    EXPECT_DOUBLE_EQ(result2->getBrakePercent().value(), 0.0);
    // Steering updated by frame 2
    EXPECT_NEAR(result2->getSteeringAngleDeg().value(), 100.0, 0.1);
}

// Test 17: VehicleSignal timestamp reflects the MockTimeProvider value
TEST_F(CANSignalDecoderBaseTest, TimestampReflectsTimeProvider)
{
    constexpr uint64_t expectedTimestamp = 12345;

    auto decoderOwner = std::unique_ptr<TestableDecoder>{};

    CANSignalDecoderBase::DecoderMap map;
    map[CAN_ID_280] = [&decoderOwner](const std::vector<uint8_t>& data) {
        auto [throttle, brake] = TestableDecoder::decode280(data);
        decoderOwner->setThrottlePercent(throttle);
        decoderOwner->setBrakePercent(brake);
    };

    decoderOwner = std::make_unique<TestableDecoder>(
        std::make_unique<MockTimeProvider>(expectedTimestamp),
        std::move(map));

    auto frame = makeFrame(CAN_ID_280, std::vector<uint8_t>(8, 0));
    auto result = decoderOwner->translateFrame(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->getTimestampUtcMs(), expectedTimestamp);
}
