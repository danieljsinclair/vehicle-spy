#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "vehicle-sim/domain/CANTranslatorBase.h"
#include "vehicle-sim/domain/ITimeProvider.h"

using namespace vehicle_sim::domain;

// ================================================
// Test double for CANTranslatorBase
// Supports CAN IDs 280 (DI_systemStatus) and 297 (SCCM_steeringAngleSensor)
// ================================================

class TestCANTranslator : public CANTranslatorBase {
public:
    using CANTranslatorBase::CANTranslatorBase;

    bool isKnownCANId(uint16_t id) const noexcept override {
        return id == CAN_ID_DI_SYSTEM || id == CAN_ID_SCCM_STEER;
    }

    void decodeFrame(uint16_t canId, const std::vector<uint8_t>& data) const override {
        switch (canId) {
            case CAN_ID_DI_SYSTEM:
                decodeDISystem(data);
                break;
            case CAN_ID_SCCM_STEER:
                decodeSCCMSteering(data);
                break;
        }
    }

    using CANTranslatorBase::extractCANId;
    using CANTranslatorBase::BRAKE_PEDAL_PRESSED_PERCENT;
};

// ================================================
// Test fixture with helper methods
// ================================================

class CANTranslatorBaseTest : public ::testing::Test {
protected:
    std::shared_ptr<MockTimeProvider> mockTimeProvider_ = std::make_shared<MockTimeProvider>(1234567890ULL);
    TestCANTranslator translator_{mockTimeProvider_};

    std::vector<uint8_t> canFrame_{0, 0, 0, 0, 0, 0, 0, 0};

    std::vector<uint8_t> frameWithId(uint16_t canId, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>(canId & 0xFF));
        frame.push_back(static_cast<uint8_t>((canId >> 8) & 0xFF));
        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }
};

// ================================================
// extractCANId tests
// ================================================

TEST_F(CANTranslatorBaseTest, ExtractsCANIdLittleEndian) {
    std::vector<uint8_t> frame{0x18, 0x01};  // 280 = 0x118, little-endian
    uint16_t id = TestCANTranslator::extractCANId(frame);
    EXPECT_EQ(id, 280);
}

TEST_F(CANTranslatorBaseTest, ExtractsZeroCANId) {
    std::vector<uint8_t> frame{0x00, 0x00};
    uint16_t id = TestCANTranslator::extractCANId(frame);
    EXPECT_EQ(id, 0);
}

TEST_F(CANTranslatorBaseTest, ExtractsMaxUint16CANId) {
    std::vector<uint8_t> frame{0xFF, 0xFF};
    uint16_t id = TestCANTranslator::extractCANId(frame);
    EXPECT_EQ(id, 65535);
}

TEST_F(CANTranslatorBaseTest, ReturnsZeroForTooShortFrame) {
    std::vector<uint8_t> frame{0x18};
    uint16_t id = TestCANTranslator::extractCANId(frame);
    EXPECT_EQ(id, 0);
}

// ================================================
// decodeDISystem tests (CAN 280)
// ================================================

TEST_F(CANTranslatorBaseTest, DecodeDISystemUpdatesThrottleState) {
    canFrame_[4] = 125;  // 50% / 0.4 = 125

    auto frame = frameWithId(280, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 50.0, 0.5);
}

TEST_F(CANTranslatorBaseTest, DecodeDISystemUpdatesBrakeStateWhenPressed) {
    canFrame_[2] = 0x02;  // DI_brakePedalState = 1 at bit 17

    auto frame = frameWithId(280, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getBrakePercent().value(), 50.0);
}

TEST_F(CANTranslatorBaseTest, DecodeDISystemDoesNotOverwriteHigherBrakeValue) {
    // First frame: brake pedal pressed
    canFrame_[2] = 0x02;
    auto frame1 = frameWithId(280, canFrame_);
    [[maybe_unused]] auto r1 = translator_.translate(frame1);

    // Second frame: brake state = 0 (should keep 50%)
    std::vector<uint8_t> frame2Data{0, 0, 0, 0, 0, 0, 0, 0};
    auto frame2 = frameWithId(280, frame2Data);
    auto result = translator_.translate(frame2);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getBrakePercent().value(), 50.0);
}

// ================================================
// decodeSCCMSteering tests (CAN 297)
// ================================================

TEST_F(CANTranslatorBaseTest, DecodeSCCMSteeringUpdatesSteeringState) {
    // 45 deg: raw = (45 + 819.2) / 0.1 = 8642
    canFrame_[2] = 0xC2;
    canFrame_[3] = 0x21;

    auto frame = frameWithId(297, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 45.0, 1.0);
}

TEST_F(CANTranslatorBaseTest, DecodeSCCMSteeringAtCenter) {
    // 0 deg: raw = 8192
    canFrame_[2] = 0x00;
    canFrame_[3] = 0x20;

    auto frame = frameWithId(297, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 0.0, 0.5);
}

// ================================================
// buildSignal with ITimeProvider tests
// ================================================

TEST_F(CANTranslatorBaseTest, BuildSignalUsesInjectedTimeProvider) {
    mockTimeProvider_->setCurrentTimeMs(9876543210ULL);

    canFrame_[4] = 75;  // 30% throttle
    auto frame = frameWithId(280, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->getTimestampUtcMs(), 9876543210ULL);
}

TEST_F(CANTranslatorBaseTest, BuildSignalPassesAllStateFields) {
    canFrame_[4] = 100;  // 40% throttle
    canFrame_[2] = 0x02;  // Brake pressed

    auto frame1 = frameWithId(280, canFrame_);
    [[maybe_unused]] auto r1 = translator_.translate(frame1);

    std::vector<uint8_t> steerData{0, 0, 0x84, 0x23, 0, 0, 0, 0};  // 90 deg right
    auto frame2 = frameWithId(297, steerData);
    auto result = translator_.translate(frame2);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 40.0, 0.5);
    EXPECT_DOUBLE_EQ(result->getBrakePercent().value(), 50.0);
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 90.0, 1.0);
    EXPECT_EQ(result->getTimestampUtcMs(), 1234567890ULL);
}

// ================================================
// translate Template Method tests
// ================================================

TEST_F(CANTranslatorBaseTest, TranslateReturnsNulloptForTooShortFrame) {
    std::vector<uint8_t> shortFrame{0x00, 0x01};
    auto result = translator_.translate(shortFrame);

    EXPECT_FALSE(result.has_value());
}

TEST_F(CANTranslatorBaseTest, TranslateReturnsNulloptForUnknownCANId) {
    auto frame = frameWithId(999, canFrame_);
    auto result = translator_.translate(frame);

    EXPECT_FALSE(result.has_value());
}

TEST_F(CANTranslatorBaseTest, TranslateCallsDecodeFrameForValidCAN280) {
    canFrame_[4] = 150;  // 60% throttle
    auto frame = frameWithId(280, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 60.0, 0.5);
}

TEST_F(CANTranslatorBaseTest, TranslateCallsDecodeFrameForValidCAN297) {
    canFrame_[2] = 0x7C;
    canFrame_[3] = 0x1C;  // -90 deg left

    auto frame = frameWithId(297, canFrame_);
    auto result = translator_.translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), -90.0, 1.0);
}

TEST_F(CANTranslatorBaseTest, TranslateAccumulatesStateAcrossMultipleCalls) {
    // Frame 1: throttle 50%
    std::vector<uint8_t> accelData{0, 0, 0, 0, 125, 0, 0, 0};
    auto frame1 = frameWithId(280, accelData);
    [[maybe_unused]] auto r1 = translator_.translate(frame1);

    // Frame 2: steering 30 deg
    // 30 deg: raw = (30 + 819.2) / 0.1 = 8492
    std::vector<uint8_t> steerData{0, 0, 0x2C, 0x21, 0, 0, 0, 0};
    auto frame2 = frameWithId(297, steerData);
    auto result = translator_.translate(frame2);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 50.0, 0.5);
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 30.0, 1.0);
}

// ================================================
// isValidPacket tests
// ================================================

TEST_F(CANTranslatorBaseTest, IsValidPacketReturnsFalseForTooShortFrame) {
    std::vector<uint8_t> shortFrame{0x00, 0x01, 0x00};
    EXPECT_FALSE(translator_.isValidPacket(shortFrame));
}

TEST_F(CANTranslatorBaseTest, IsValidPacketReturnsFalseForUnknownCANId) {
    auto frame = frameWithId(999, canFrame_);
    EXPECT_FALSE(translator_.isValidPacket(frame));
}

TEST_F(CANTranslatorBaseTest, IsValidPacketReturnsTrueForKnownCANId280) {
    auto frame = frameWithId(280, canFrame_);
    EXPECT_TRUE(translator_.isValidPacket(frame));
}

TEST_F(CANTranslatorBaseTest, IsValidPacketReturnsTrueForKnownCANId297) {
    auto frame = frameWithId(297, canFrame_);
    EXPECT_TRUE(translator_.isValidPacket(frame));
}
