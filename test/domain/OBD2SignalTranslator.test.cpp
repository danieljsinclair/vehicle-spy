#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// OBD2SignalTranslator Tests
// TDD RED Phase — tests assert correct behaviour
// Standard SAE J1979 OBD2 response parsing
// ================================================

class OBD2SignalTranslatorTest : public ::testing::Test {
protected:
    OBD2SignalTranslator translator;
};

// Helper: build a standard OBD2 Mode 01 response
// Format: [0x41] [PID] [data bytes...]
static std::vector<uint8_t> makeOBD2Response(uint8_t pid, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> response;
    response.push_back(0x41);  // Mode 01 response
    response.push_back(pid);
    response.insert(response.end(), data.begin(), data.end());
    return response;
}

// ================================================
// Validation Tests
// ================================================

TEST_F(OBD2SignalTranslatorTest, ValidOBD2ResponsePassesValidation) {
    // Standard response: mode 0x41, PID 0x0D (speed), 1 data byte
    auto response = makeOBD2Response(0x0D, {0x64});  // 100 km/h
    EXPECT_TRUE(translator.isValidPacket(response));
}

TEST_F(OBD2SignalTranslatorTest, TooShortFailsValidation) {
    // Need at least 3 bytes: mode + pid + 1 data byte
    std::vector<uint8_t> tooShort = {0x41, 0x0D};
    EXPECT_FALSE(translator.isValidPacket(tooShort));
}

TEST_F(OBD2SignalTranslatorTest, NonResponseModeFailsValidation) {
    // 0x01 is a request mode, not a response
    auto response = makeOBD2Response(0x0D, {0x64});
    response[0] = 0x01;
    EXPECT_FALSE(translator.isValidPacket(response));
}

TEST_F(OBD2SignalTranslatorTest, EmptyPacketFailsValidation) {
    std::vector<uint8_t> empty;
    EXPECT_FALSE(translator.isValidPacket(empty));
}

// ================================================
// Speed Translation (PID 0x0D)
// ================================================

TEST_F(OBD2SignalTranslatorTest, TranslatesVehicleSpeed) {
    // PID 0x0D: A = speed in km/h
    auto response = makeOBD2Response(0x0D, {100});  // 100 km/h
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getSpeedKmh().value(), 100.0);
}

TEST_F(OBD2SignalTranslatorTest, TranslatesZeroSpeed) {
    auto response = makeOBD2Response(0x0D, {0x00});
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getSpeedKmh().value(), 0.0);
}

// ================================================
// Throttle Translation (PID 0x11)
// ================================================

TEST_F(OBD2SignalTranslatorTest, TranslatesThrottlePosition) {
    // PID 0x11: (A / 255) * 100
    auto response = makeOBD2Response(0x11, {0x80});  // 128/255 * 100 ≈ 50.2%
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    double expected = (128.0 / 255.0) * 100.0;
    EXPECT_NEAR(result->getThrottlePercent().value(), expected, 0.1);
}

TEST_F(OBD2SignalTranslatorTest, TranslatesFullThrottle) {
    auto response = makeOBD2Response(0x11, {0xFF});  // 100%
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getThrottlePercent().value(), 100.0);
}

// ================================================
// Engine Load Translation (PID 0x04)
// ================================================

TEST_F(OBD2SignalTranslatorTest, TranslatesEngineLoadAsAcceleration) {
    // PID 0x04: engine load maps to acceleration proxy in VehicleSignal
    // (A / 255) * 100, scaled to G range
    auto response = makeOBD2Response(0x04, {0x80});  // ~50% load
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    // Engine load used as a proxy — not exact acceleration
    EXPECT_GT(result->getAccelerationG().value(), -5.0);
    EXPECT_LT(result->getAccelerationG().value(), 5.0);
}

// ================================================
// Multi-PID Aggregation
// ================================================

TEST_F(OBD2SignalTranslatorTest, AggregatesMultiplePIDsIntoSingleSignal) {
    // Feed throttle response, then speed response
    auto throttleResp = makeOBD2Response(0x11, {0x80});  // ~50%
    auto speedResp = makeOBD2Response(0x0D, {0x64});     // 100 km/h

    auto r1 = translator.translate(throttleResp);
    ASSERT_TRUE(r1.has_value());

    auto r2 = translator.translate(speedResp);
    ASSERT_TRUE(r2.has_value());

    // Second response should contain aggregated state:
    // throttle from first + speed from second
    EXPECT_NEAR(r2->getThrottlePercent().value(), (128.0 / 255.0) * 100.0, 0.1);
    EXPECT_DOUBLE_EQ(r2->getSpeedKmh().value(), 100.0);
}

// ================================================
// Unknown PID Handling
// ================================================

TEST_F(OBD2SignalTranslatorTest, UnknownPIDReturnsPartialSignal) {
    // Unknown PID — translator should still return a signal using last-known values
    auto response = makeOBD2Response(0xFF, {0x42});  // Unknown PID
    auto result = translator.translate(response);

    // Should return a signal (not nullopt) — just with default/last-known values
    ASSERT_TRUE(result.has_value());
}

// ================================================
// Edge Cases
// ================================================

TEST_F(OBD2SignalTranslatorTest, MaxSpeedValue) {
    // Max single-byte speed: 255 km/h
    auto response = makeOBD2Response(0x0D, {0xFF});
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->getSpeedKmh().value(), 255.0);
}

TEST_F(OBD2SignalTranslatorTest, TimestampIsPopulated) {
    auto response = makeOBD2Response(0x0D, {0x64});
    auto result = translator.translate(response);

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->getTimestampUtcMs(), 0u);
}
