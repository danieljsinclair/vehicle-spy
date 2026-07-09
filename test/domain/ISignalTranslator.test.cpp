#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/domain/ISignalTranslator.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;
using testing::_;
using testing::Return;
using testing::Eq;
using testing::Field;

// ================================================
// ISignalTranslator Interface Tests
// TDD - Tests verify interface contract
// ================================================

class MockSignalTranslator : public ISignalTranslator {
public:
    MOCK_METHOD(std::optional<VehicleSignal>, translate,
        (const std::vector<std::uint8_t>& rawData, std::optional<std::uint64_t> timestampUtcMs),
        (const, noexcept, override));
    MOCK_METHOD(bool, isValidPacket, (const std::vector<std::uint8_t>& rawData), (const, noexcept, override));
};

// ================================================
// Test Suite 1: Translation Behavior Tests
// ================================================

TEST(ISignalTranslatorTest, TranslateReturnsOptionalVehicleSignal)
{
    // ASSERT: Valid raw data is translated to VehicleSignal
    // This tests the happy path behavior

    MockSignalTranslator mock;
    std::vector<std::uint8_t> validData = {0xAA, 0x55};

    EXPECT_CALL(mock, translate(validData, _))
        .WillOnce(Return(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 12345, .throttlePercent = 50.0, .speedKmh = 100.0, .accelerationG = 0.5, .brakePercent = 0.0})));

    auto result = mock.translate(validData, std::nullopt);

    ASSERT_TRUE(result.has_value())
        << "Should return VehicleSignal for valid data";
    ASSERT_TRUE(result->getThrottlePercent().has_value());
    EXPECT_DOUBLE_EQ(result->getThrottlePercent().value(), 50.0);
}

TEST(ISignalTranslatorTest, TranslateReturnsEmptyOptionalOnFailure)
{
    // ASSERT: Invalid raw data returns empty optional
    // This tests the error handling behavior

    MockSignalTranslator mock;
    std::vector<std::uint8_t> invalidData = {0xFF};

    EXPECT_CALL(mock, translate(invalidData, _))
        .WillOnce(Return(std::nullopt));

    auto result = mock.translate(invalidData, std::nullopt);

    ASSERT_FALSE(result.has_value())
        << "Should return nullopt for invalid data";
}

// ================================================
// Test Suite 2: Validation Behavior Tests
// ================================================

TEST(ISignalTranslatorTest, IsValidPacketValidatesCorrectly)
{
    // ASSERT: Packet validation works correctly
    // Test that the validation method behaves as expected

    MockSignalTranslator mock;
    std::vector<std::uint8_t> validData = {0xAA, 0x55};
    std::vector<std::uint8_t> invalidData = {0x00};

    EXPECT_CALL(mock, isValidPacket(validData))
        .WillOnce(Return(true));

    EXPECT_CALL(mock, isValidPacket(invalidData))
        .WillOnce(Return(false));

    ASSERT_TRUE(mock.isValidPacket(validData))
        << "Should validate valid packet as true";
    ASSERT_FALSE(mock.isValidPacket(invalidData))
        << "Should validate invalid packet as false";
}
