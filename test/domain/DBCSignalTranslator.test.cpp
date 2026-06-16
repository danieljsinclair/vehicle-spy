#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"

using namespace vehicle_sim::domain;

class DBCSignalTranslatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Construct VehicleConfig with mappings matching real Model3CAN.dbc
        config_ = std::make_unique<VehicleConfig>(
            "tesla_model3.dbc",
            "tesla_model3.dbc",
            "Tesla Model Y",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DI_accelPedalPos", "throttlePercent"}
            }
        );

        // Construct DBCParseResult with real DBC signal definitions
        // CAN 264 (0x108): DIR_axleSpeed (bit 40, 16-bit, Intel, scale 0.1, signed, range [-2750|2750])
        parseResult_.signalsByCanId[264].emplace_back(DBCSignalParams{
            264, "DIR_axleSpeed", 40, 16, DBCByteOrder::Intel, 0.1, 0.0, true, "RPM", -2750.0, 2750.0
        });

        // CAN 280 (0x118): DI_accelPedalPos (bit 32, 8-bit, Intel @1+, scale 0.4, unsigned, range [0|100])
        parseResult_.signalsByCanId[280].emplace_back(DBCSignalParams{
            280, "DI_accelPedalPos", 32, 8, DBCByteOrder::Intel, 0.4, 0.0, false, "%", 0.0, 100.0
        });

        translator_ = std::make_unique<DBCSignalTranslator>(*config_, parseResult_);
    }

    std::unique_ptr<VehicleConfig> config_;
    DBCParseResult parseResult_;
    std::unique_ptr<DBCSignalTranslator> translator_;
};

TEST_F(DBCSignalTranslatorTest, SingleCANFrameProducesSignal) {
    // CAN 264 with axleSpeed = 25000 raw -> 2500.0 RPM (25000 * 0.1)
    // Frame format: [canId_lo, canId_hi, data_byte_0, ..., data_byte_7]
    // 264 = 0x0108 = [0x08, 0x01]
    // axleSpeed at bit 40, 16-bit Intel -> bytes 5,6 (0-indexed in data payload)
    // 25000 = 0x61A8 -> [0x00, 0xA8, 0x61] in data payload
    std::vector<std::uint8_t> frame = {
        0x08, 0x01,  // CAN ID 264 (little-endian)
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00  // 8 data bytes
    };

    auto result = translator_->translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->getMotorRpm().value(), 2500.0);
    EXPECT_GT(result->getTimestampUtcMs(), 0);
}

TEST_F(DBCSignalTranslatorTest, MultipleCANFramesAccumulateState) {
    // First frame: CAN 264 with axleSpeed = 25000 -> 2500.0 RPM
    std::vector<std::uint8_t> frame1 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00
    };

    // Second frame: CAN 280 with accelPedalPos = 3 -> 1.2% (3 * 0.4)
    // 280 = 0x0118 = [0x18, 0x01]
    // accelPedalPos at bit 32, 8-bit Intel -> byte 4 (32/8)
    std::vector<std::uint8_t> frame2 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00  // byte 4 = 0x03
    };

    // Third frame: CAN 264 with new axleSpeed = 20000 -> 2000.0 RPM
    // 20000 = 0x4E20, Intel: byte 5 = 0x20, byte 6 = 0x4E (fits in signed 16-bit)
    std::vector<std::uint8_t> frame3 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4E, 0x00
    };

    auto result1 = translator_->translate(frame1);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->getMotorRpm().value(), 2500.0);

    auto result2 = translator_->translate(frame2);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(result2->getThrottlePercent().value(), 1.2, 0.01);

    auto result3 = translator_->translate(frame3);
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3->getMotorRpm().value(), 2000.0);
    EXPECT_NEAR(result3->getThrottlePercent().value(), 1.2, 0.01);
}

TEST_F(DBCSignalTranslatorTest, UnknownCANIdIgnored) {
    // First frame: known CAN 264
    std::vector<std::uint8_t> frame1 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00
    };

    // Second frame: unknown CAN 999
    std::vector<std::uint8_t> frame2 = {
        0xE7, 0x03,  // CAN ID 999 (0x03E7)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    auto result1 = translator_->translate(frame1);
    ASSERT_TRUE(result1.has_value());

    auto result2 = translator_->translate(frame2);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->getMotorRpm().value(), 2500.0);
}

TEST_F(DBCSignalTranslatorTest, FrameTooShortReturnsNullopt) {
    std::vector<std::uint8_t> shortFrame = {0x08, 0x01, 0x00, 0x00};  // Only 4 bytes

    auto result = translator_->translate(shortFrame);

    EXPECT_FALSE(result.has_value());
}

TEST_F(DBCSignalTranslatorTest, RepeatedFrameOverwritesState) {
    // First frame: CAN 264 with axleSpeed = 25000 -> 2500.0 RPM
    std::vector<std::uint8_t> frame1 = {
        0x08, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00
    };

    // Second frame: CAN 264 with axleSpeed = 10000 -> 1000.0 RPM
    // 10000 = 0x2710, Intel: byte 5 = 0x10, byte 6 = 0x27
    std::vector<std::uint8_t> frame2 = {
        0x08, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00
    };

    auto result1 = translator_->translate(frame1);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->getMotorRpm().value(), 2500.0);

    auto result2 = translator_->translate(frame2);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->getMotorRpm().value(), 1000.0);
}

TEST_F(DBCSignalTranslatorTest, IsValidPacket_BasicValidation) {
    std::vector<std::uint8_t> validFrame = {
        0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00, 0x00
    };

    std::vector<std::uint8_t> shortFrame = {0x08, 0x01, 0x00};

    EXPECT_TRUE(translator_->isValidPacket(validFrame));
    EXPECT_FALSE(translator_->isValidPacket(shortFrame));
}

TEST_F(DBCSignalTranslatorTest, GetSupportedCANIds) {
    auto ids = translator_->getSupportedCANIds();

    EXPECT_EQ(ids.size(), 2);
    EXPECT_NE(std::find(ids.begin(), ids.end(), 264), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), 280), ids.end());
}

TEST_F(DBCSignalTranslatorTest, TranslateStampsCaptureTimeWhenProvided) {
    // Replay path: caller supplies the original capture timestamp (epoch ms).
    // translate() must echo it into the VehicleSignal, not wall-clock now().
    std::vector<std::uint8_t> frame = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00
    };
    constexpr std::uint64_t captureTs = 1781472526915ULL;

    auto result = translator_->translate(frame, captureTs);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->getTimestampUtcMs(), captureTs);
}

TEST_F(DBCSignalTranslatorTest, TranslateFallsBackToWallClockWhenNoTimestamp) {
    // Live/default path: no capture timestamp supplied -> wall-clock fallback.
    std::vector<std::uint8_t> frame = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00
    };
    const auto before = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    auto result = translator_->translate(frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->getTimestampUtcMs(), before);
}

TEST_F(DBCSignalTranslatorTest, ResetClearsAccumulatedState) {
    // First frame: CAN 264 with axleSpeed = 25000 -> 2500.0 RPM
    std::vector<std::uint8_t> frame1 = {
        0x08, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00
    };

    (void)translator_->translate(frame1);
    translator_->reset();

    // Second frame: CAN 280 with accelPedalPos = 3 -> 1.2%
    // Intel bit 32 -> byte 4
    std::vector<std::uint8_t> frame2 = {
        0x18, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00
    };

    auto result = translator_->translate(frame2);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->getMotorRpm().has_value());  // Reset cleared previous state
    EXPECT_NEAR(result->getThrottlePercent().value(), 1.2, 0.01);
}
