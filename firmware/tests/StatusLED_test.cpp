// StatusLED_test.cpp - Tests for StatusLED vanilla class

#include <gtest/gtest.h>
#include "can-bridge/StatusLED.h"
#include "can-bridge/IStatusLEDOutput.h"
#include "mocks/ArduinoMock.h"

using namespace firmware;

// Mock StatusLEDOutput for testing
class MockStatusLEDOutput : public IStatusLEDOutput {
public:
    bool ledOn{false};
    bool initCalled{false};

    void setOn(bool on) override {
        ledOn = on;
    }

    void init() override {
        initCalled = true;
    }

    void reset() {
        ledOn = false;
        initCalled = false;
    }
};

class StatusLEDTest : public ::testing::Test {
protected:
    MockStatusLEDOutput outputMock;
    std::unique_ptr<StatusLED> statusLed;

    void SetUp() override {
        outputMock.reset();
        arduino_mock::resetAllMocks();
    }

    void TearDown() override {
        statusLed.reset();
    }
};

// Initialization tests
TEST_F(StatusLEDTest, Init_CallsOutputInit) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->init();

    EXPECT_TRUE(outputMock.initCalled);
}

TEST_F(StatusLEDTest, Init_SetsBootPattern) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->init();

    EXPECT_EQ(statusLed->getPattern(), StatusLED::Pattern::BOOT);
}

TEST_F(StatusLEDTest, Init_TurnsLedOffInitially) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->init();

    // After init, LED should be off (first step of BOOT pattern is ON, but that's set on first update)
    // The init() comment says "Turn LED OFF first" then set BOOT pattern
    // Actually looking at StatusLED::init(), it calls setLedOn(false) then setPattern(BOOT)
    // So LED should be OFF initially
    EXPECT_FALSE(outputMock.ledOn);
}

// Pattern setting tests
TEST_F(StatusLEDTest, SetPattern_ChangesPattern) {
    statusLed = std::make_unique<StatusLED>(&outputMock);

    statusLed->setPattern(StatusLED::Pattern::WIFI_CONNECTED);
    EXPECT_EQ(statusLed->getPattern(), StatusLED::Pattern::WIFI_CONNECTED);

    statusLed->setPattern(StatusLED::Pattern::CLIENT_CONNECTED);
    EXPECT_EQ(statusLed->getPattern(), StatusLED::Pattern::CLIENT_CONNECTED);
}

TEST_F(StatusLEDTest, SetPattern_OffPattern) {
    statusLed = std::make_unique<StatusLED>(&outputMock);

    statusLed->setPattern(StatusLED::Pattern::OFF);
    EXPECT_EQ(statusLed->getPattern(), StatusLED::Pattern::OFF);
}

// Pattern step retrieval tests (white-box testing)
TEST_F(StatusLEDTest, GetPatternSteps_OffPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::OFF);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(steps[0].state, LEDState::OFF);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::SEPARATOR_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_BootPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::BOOT);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(steps[0].state, LEDState::ON);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::MED_FLASH_MS);
    EXPECT_EQ(steps[1].state, LEDState::OFF);
    EXPECT_EQ(steps[1].durationMs, StatusLEDConstants::MED_GAP_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_WifiSearchingPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::WIFI_SEARCHING);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(steps[0].state, LEDState::ON);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::TINY_FLASH_MS);
    EXPECT_EQ(steps[1].state, LEDState::OFF);
    EXPECT_EQ(steps[1].durationMs, StatusLEDConstants::SEARCHING_GAP_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_WifiConnectedPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::WIFI_CONNECTED);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(steps[0].state, LEDState::ON);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::LONG_FLASH_MS);
    EXPECT_EQ(steps[1].state, LEDState::OFF);
    EXPECT_EQ(steps[1].durationMs, StatusLEDConstants::SHORT_GAP_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_ClientConnectedPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::CLIENT_CONNECTED);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(steps[0].state, LEDState::ON);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::SEPARATOR_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_ApModePattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::AP_MODE);
    EXPECT_EQ(count, 6u);
    // LONG_FLASH ON, TINY_GAP OFF, TINY_FLASH ON, TINY_GAP OFF, TINY_FLASH ON, SEPARATOR
    EXPECT_EQ(steps[0].state, LEDState::ON);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::LONG_FLASH_MS);
    EXPECT_EQ(steps[1].state, LEDState::OFF);
    EXPECT_EQ(steps[1].durationMs, StatusLEDConstants::TINY_GAP_MS);
    EXPECT_EQ(steps[2].state, LEDState::ON);
    EXPECT_EQ(steps[2].durationMs, StatusLEDConstants::TINY_FLASH_MS);
    EXPECT_EQ(steps[3].state, LEDState::OFF);
    EXPECT_EQ(steps[3].durationMs, StatusLEDConstants::TINY_GAP_MS);
    EXPECT_EQ(steps[4].state, LEDState::ON);
    EXPECT_EQ(steps[4].durationMs, StatusLEDConstants::TINY_FLASH_MS);
    EXPECT_EQ(steps[5].state, LEDState::OFF);
    EXPECT_EQ(steps[5].durationMs, StatusLEDConstants::SEPARATOR_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_OtaInProgressPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::OTA_IN_PROGRESS);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(steps[0].state, LEDState::ON);
    EXPECT_EQ(steps[0].durationMs, StatusLEDConstants::SHORT_FLASH_MS);
    EXPECT_EQ(steps[1].state, LEDState::OFF);
    EXPECT_EQ(steps[1].durationMs, StatusLEDConstants::SHORT_GAP_MS);
}

TEST_F(StatusLEDTest, GetPatternSteps_ErrorPatterns) {
    // AUTH_FAILURE: 3 short pulses + 2 tiny pulses + separator = 11 steps
    auto [stepsAuth, countAuth] = StatusLED::getPatternSteps(StatusLED::Pattern::AUTH_FAILURE);
    EXPECT_EQ(countAuth, 11u);

    // ERROR_RECOVERABLE: 3 short pulses + 3 tiny pulses + separator = 13 steps
    auto [stepsRec, countRec] = StatusLED::getPatternSteps(StatusLED::Pattern::ERROR_RECOVERABLE);
    EXPECT_EQ(countRec, 13u);

    // ERROR_NO_NTP_SERVICE: 3 short pulses + 1 tiny pulse + separator = 9 steps
    auto [stepsNtp, countNtp] = StatusLED::getPatternSteps(StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    EXPECT_EQ(countNtp, 9u);
}

TEST_F(StatusLEDTest, GetPatternSteps_FatalPattern) {
    auto [steps, count] = StatusLED::getPatternSteps(StatusLED::Pattern::FATAL_UNRECOVERABLE);
    // SOS: 3 short, 3 long, 3 short, separator = 19 steps
    EXPECT_EQ(count, 19u);
}

// Update state machine tests
TEST_F(StatusLEDTest, Update_OffPattern_StaysOff) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->setPattern(StatusLED::Pattern::OFF);

    statusLed->update(0);
    EXPECT_FALSE(outputMock.ledOn);

    statusLed->update(1000);
    EXPECT_FALSE(outputMock.ledOn);

    statusLed->update(5000);
    EXPECT_FALSE(outputMock.ledOn);
}

TEST_F(StatusLEDTest, Update_ClientConnectedPattern_StaysOn) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->setPattern(StatusLED::Pattern::CLIENT_CONNECTED);

    statusLed->update(0);
    EXPECT_TRUE(outputMock.ledOn);

    statusLed->update(1000);
    EXPECT_TRUE(outputMock.ledOn);
}

TEST_F(StatusLEDTest, Update_BootPattern_Toggles) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->setPattern(StatusLED::Pattern::BOOT);

    // First update: pattern resets to first step (MED_FLASH ON)
    statusLed->update(0);
    EXPECT_TRUE(outputMock.ledOn);

    // Still in ON phase
    statusLed->update(StatusLEDConstants::MED_FLASH_MS - 1);
    EXPECT_TRUE(outputMock.ledOn);

    // Transition to OFF
    statusLed->update(StatusLEDConstants::MED_FLASH_MS);
    EXPECT_FALSE(outputMock.ledOn);

    // Still in OFF phase
    statusLed->update(StatusLEDConstants::MED_FLASH_MS + StatusLEDConstants::MED_GAP_MS - 1);
    EXPECT_FALSE(outputMock.ledOn);

    // Cycle back to ON
    statusLed->update(StatusLEDConstants::MED_FLASH_MS + StatusLEDConstants::MED_GAP_MS);
    EXPECT_TRUE(outputMock.ledOn);
}

TEST_F(StatusLEDTest, Update_WifiSearchingPattern_Toggles) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->setPattern(StatusLED::Pattern::WIFI_SEARCHING);

    statusLed->update(0);
    EXPECT_TRUE(outputMock.ledOn);

    statusLed->update(StatusLEDConstants::TINY_FLASH_MS);
    EXPECT_FALSE(outputMock.ledOn);

    statusLed->update(StatusLEDConstants::TINY_FLASH_MS + StatusLEDConstants::SEARCHING_GAP_MS);
    EXPECT_TRUE(outputMock.ledOn);
}

TEST_F(StatusLEDTest, Update_ApModePattern_FullCycle) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->setPattern(StatusLED::Pattern::AP_MODE);

    uint32_t t = 0;

    // Step 1: LONG_FLASH ON
    statusLed->update(t);
    EXPECT_TRUE(outputMock.ledOn);

    // Step 2: TINY_GAP OFF
    t += StatusLEDConstants::LONG_FLASH_MS;
    statusLed->update(t);
    EXPECT_FALSE(outputMock.ledOn);

    // Step 3: TINY_FLASH ON
    t += StatusLEDConstants::TINY_GAP_MS;
    statusLed->update(t);
    EXPECT_TRUE(outputMock.ledOn);

    // Step 4: TINY_GAP OFF
    t += StatusLEDConstants::TINY_FLASH_MS;
    statusLed->update(t);
    EXPECT_FALSE(outputMock.ledOn);

    // Step 5: TINY_FLASH ON
    t += StatusLEDConstants::TINY_GAP_MS;
    statusLed->update(t);
    EXPECT_TRUE(outputMock.ledOn);

    // Step 6: SEPARATOR OFF (end of cycle)
    t += StatusLEDConstants::TINY_FLASH_MS;
    statusLed->update(t);
    EXPECT_FALSE(outputMock.ledOn);

    // Cycle back to beginning (LONG_FLASH ON)
    t += StatusLEDConstants::SEPARATOR_MS;
    statusLed->update(t);
    EXPECT_TRUE(outputMock.ledOn);
}

TEST_F(StatusLEDTest, Update_PatternChange_ResetsImmediately) {
    statusLed = std::make_unique<StatusLED>(&outputMock);

    // Start with BOOT pattern
    statusLed->setPattern(StatusLED::Pattern::BOOT);
    statusLed->update(0);
    EXPECT_TRUE(outputMock.ledOn);

    // Mid-cycle, switch to CLIENT_CONNECTED
    statusLed->update(StatusLEDConstants::MED_FLASH_MS);
    EXPECT_FALSE(outputMock.ledOn);

    // Change pattern immediately (interrupts current cycle)
    statusLed->setPattern(StatusLED::Pattern::CLIENT_CONNECTED);
    statusLed->update(StatusLEDConstants::MED_FLASH_MS + 1);
    EXPECT_TRUE(outputMock.ledOn);  // CLIENT_CONNECTED is solid ON
}

TEST_F(StatusLEDTest, Update_NullOutput_DoesNotCrash) {
    statusLed = std::make_unique<StatusLED>(nullptr);
    statusLed->setPattern(StatusLED::Pattern::BOOT);

    // Should not crash with null output
    statusLed->update(0);
    statusLed->update(1000);
    statusLed->update(5000);
}

// Timing verification
TEST_F(StatusLEDTest, TimingConstants_DocumentedValues) {
    EXPECT_EQ(StatusLEDConstants::TINY_FLASH_MS, 100u);
    EXPECT_EQ(StatusLEDConstants::SHORT_FLASH_MS, 200u);
    EXPECT_EQ(StatusLEDConstants::MED_FLASH_MS, 500u);
    EXPECT_EQ(StatusLEDConstants::LONG_FLASH_MS, 800u);
    EXPECT_EQ(StatusLEDConstants::VERY_LONG_FLASH_MS, 1800u);

    EXPECT_EQ(StatusLEDConstants::TINY_GAP_MS, 100u);
    EXPECT_EQ(StatusLEDConstants::SHORT_GAP_MS, 200u);
    EXPECT_EQ(StatusLEDConstants::MED_GAP_MS, 500u);
    EXPECT_EQ(StatusLEDConstants::LONG_GAP_MS, 800u);
    EXPECT_EQ(StatusLEDConstants::VERY_LONG_GAP_MS, 2000u);
    EXPECT_EQ(StatusLEDConstants::SEPARATOR_MS, 2000u);

    EXPECT_EQ(StatusLEDConstants::SEARCHING_GAP_MS, 900u);
}

// Pattern change detection
TEST_F(StatusLEDTest, Update_SamePatternNoChange_DoesNotReset) {
    statusLed = std::make_unique<StatusLED>(&outputMock);
    statusLed->setPattern(StatusLED::Pattern::BOOT);

    statusLed->update(0);
    statusLed->update(StatusLEDConstants::MED_FLASH_MS);
    EXPECT_FALSE(outputMock.ledOn);

    // Setting same pattern should not reset
    statusLed->setPattern(StatusLED::Pattern::BOOT);

    // Continue cycle (should stay OFF, not reset to ON)
    statusLed->update(StatusLEDConstants::MED_FLASH_MS + 1);
    EXPECT_FALSE(outputMock.ledOn);
}
