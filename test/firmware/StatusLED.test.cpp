#include <gtest/gtest.h>
#include "StatusLED.h"
#include "IStatusLEDOutput.h"

#include <cstdint>
#include <vector>
#include <algorithm>

namespace {

// ── Mock Implementation of IStatusLEDOutput for Testing ──────────────────────────
class MockStatusLEDOutput : public firmware::IStatusLEDOutput {
public:
    struct Transition {
        uint32_t timestamp;
        bool state;
    };

    std::vector<Transition> transitions;
    uint32_t currentTime = 0;
    bool initialState = false;

    MockStatusLEDOutput() = default;

    void setOn(bool on) override {
        transitions.push_back({currentTime, on});
    }

    void init() override {
        // Record initial state for verification
        transitions.push_back({0, initialState});
    }

    // Helper to advance time and update LED
    void advanceTime(uint32_t deltaMs, firmware::StatusLED& led) {
        currentTime += deltaMs;
        led.update(currentTime);
    }

    // Helper to get the current state
    bool getCurrentState() const {
        if (transitions.empty()) return false;
        return transitions.back().state;
    }

    // Helper to count state transitions
    size_t getTransitionCount() const {
        return transitions.size();
    }

    // Helper to get transition at index
    Transition getTransition(size_t index) const {
        if (index < transitions.size()) {
            return transitions[index];
        }
        return {0, false};
    }

    void reset() {
        transitions.clear();
        currentTime = 0;
    }
};

// ── Helper to verify pattern sequence ───────────────────────────────────────────────
#ifdef __GNUC__
__attribute__((unused))
#endif
void verifyTransitions(const MockStatusLEDOutput& mock,
                      const std::vector<std::pair<uint32_t, bool>>& expected) {
    EXPECT_EQ(mock.transitions.size(), expected.size())
        << "Transition count mismatch";

    for (size_t i = 0; i < std::min(mock.transitions.size(), expected.size()); ++i) {
        EXPECT_EQ(mock.transitions[i].timestamp, expected[i].first)
            << "Timestamp mismatch at transition " << i;
        EXPECT_EQ(mock.transitions[i].state, expected[i].second)
            << "State mismatch at transition " << i;
    }
}

} // namespace

// ── TEST: Named Constants ────────────────────────────────────────────────────────────

TEST(StatusLEDConstants, ExactSpecValues) {
    // Verify the named constants match user spec exactly
    using namespace firmware;

    EXPECT_EQ(StatusLEDConstants::TINY_FLASH_MS, 100)   << "TINY_FLASH_MS should be 0.1s";
    EXPECT_EQ(StatusLEDConstants::SHORT_FLASH_MS, 200)  << "SHORT_FLASH_MS should be 0.2s";
    EXPECT_EQ(StatusLEDConstants::MED_FLASH_MS, 500)    << "MED_FLASH_MS should be 0.5s";
    EXPECT_EQ(StatusLEDConstants::LONG_FLASH_MS, 800)   << "LONG_FLASH_MS should be 0.8s";
    EXPECT_EQ(StatusLEDConstants::VERY_LONG_FLASH_MS, 1800) << "VERY_LONG_FLASH_MS should be 1.8s";

    EXPECT_EQ(StatusLEDConstants::TINY_GAP_MS, 100)    << "TINY_GAP_MS should be 0.1s";
    EXPECT_EQ(StatusLEDConstants::SHORT_GAP_MS, 200)   << "SHORT_GAP_MS should be 0.2s";
    EXPECT_EQ(StatusLEDConstants::MED_GAP_MS, 500)     << "MED_GAP_MS should be 0.5s";
    EXPECT_EQ(StatusLEDConstants::LONG_GAP_MS, 800)    << "LONG_GAP_MS should be 0.8s";
    EXPECT_EQ(StatusLEDConstants::VERY_LONG_GAP_MS, 2000) << "VERY_LONG_GAP_MS should be 2s";
    EXPECT_EQ(StatusLEDConstants::SEPARATOR_MS, 2000) << "SEPARATOR_MS should be 2s";
    EXPECT_EQ(StatusLEDConstants::SEARCHING_GAP_MS, 900) << "SEARCHING_GAP_MS should be 0.9s";
}

// ── TEST: Initialization ─────────────────────────────────────────────────────────────

TEST(StatusLEDTest, InitialState_Off) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();

    // LED should start OFF first (user requirement)
    EXPECT_EQ(mock.getTransitionCount(), 1);
    EXPECT_EQ(mock.getTransition(0).state, false);
}

TEST(StatusLEDTest, InitialPattern_Boot) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();

    // After init, pattern should be set to BOOT
    EXPECT_EQ(led.getPattern(), firmware::StatusLED::Pattern::BOOT);
}

// ── TEST: BOOT Pattern (MED_FLASH ON, MED_GAP OFF) ───────────────────────────────────

TEST(StatusLEDTest, BootPattern_MedFlashMedGap) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::BOOT);
    led.update(0);

    // First step: MED_FLASH (500ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should start ON";

    // After 500ms, should turn OFF
    mock.advanceTime(500, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF after MED_FLASH";

    // After MED_GAP (500ms), should turn ON again
    mock.advanceTime(500, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should cycle back ON";
}

// ── TEST: WIFI_SEARCHING Pattern (TINY_FLASH ON, SEARCHING_GAP OFF) ────────────────

TEST(StatusLEDTest, WifiSearchingPattern_TinyFlashSearchingGap) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::WIFI_SEARCHING);
    led.update(0);

    // TINY_FLASH (100ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should start ON";

    // After 100ms, should turn OFF
    mock.advanceTime(100, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF after TINY_FLASH";

    // After SEARCHING_GAP (900ms), should turn ON again
    mock.advanceTime(900, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should cycle back ON";
}

// ── TEST: WIFI_CONNECTED Pattern (LONG_FLASH ON, SHORT_GAP OFF) ────────────────────

TEST(StatusLEDTest, WifiConnectedPattern_LongFlashShortGap) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::WIFI_CONNECTED);
    led.update(0);

    // LONG_FLASH (800ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should start ON";

    // After 800ms, should turn OFF
    mock.advanceTime(800, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF after LONG_FLASH";

    // After SHORT_GAP (200ms), should turn ON again
    mock.advanceTime(200, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should cycle back ON";
}

// ── TEST: CLIENT_CONNECTED Pattern (SOLID ON) ────────────────────────────────────────

TEST(StatusLEDTest, ClientConnectedPattern_SolidOn) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::CLIENT_CONNECTED);
    led.update(0);

    // Should be ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should be solid ON";

    // Should stay ON regardless of time passing
    mock.advanceTime(1000, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should stay ON after 1s";

    mock.advanceTime(5000, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should stay ON after 6s";

    // Critical test: uniform cycling with single ON state - stays ON forever
    mock.advanceTime(86400001, led);  // Advance past 24 hours
    EXPECT_TRUE(mock.getCurrentState()) << "Should stay ON after 24+ hours (cycling but always ON)";

    // Should stay on indefinitely through many cycles
    mock.advanceTime(100000000, led);  // Advance additional ~27 hours
    EXPECT_TRUE(mock.getCurrentState()) << "Should stay ON indefinitely (cycling but always ON)";
}

// ── TEST: AP_MODE Pattern ───────────────────────────────────────────────────────────

TEST(StatusLEDTest, ApModePattern_LongTinyTinySeparator) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::AP_MODE);
    led.update(0);

    // LONG_FLASH (800ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should start with LONG_FLASH ON";

    // After 800ms, TINY_GAP (100ms) OFF
    mock.advanceTime(800, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in TINY_GAP";

    // After 100ms, TINY_FLASH (100ms) ON
    mock.advanceTime(100, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should be ON for first TINY_FLASH";

    // After 100ms, TINY_GAP (100ms) OFF
    mock.advanceTime(100, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in second TINY_GAP";

    // After 100ms, TINY_FLASH (100ms) ON
    mock.advanceTime(100, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should be ON for second TINY_FLASH";

    // After 100ms, SEPARATOR (2000ms) OFF
    mock.advanceTime(100, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in SEPARATOR";
}

// ── TEST: OTA_IN_PROGRESS Pattern ────────────────────────────────────────────────────

TEST(StatusLEDTest, OtaInProgressPattern_ShortFlashShortGap) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::OTA_IN_PROGRESS);
    led.update(0);

    // SHORT_FLASH (200ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should start ON";

    // After 200ms, SHORT_GAP (200ms) OFF
    mock.advanceTime(200, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF after SHORT_FLASH";

    // After 200ms, should cycle back ON
    mock.advanceTime(200, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should cycle back ON";
}

// ── TEST: AUTH_FAILURE Pattern (ERROR_3_PULSE + 2×TINY_PULSE + SEPARATOR) ────────────

TEST(StatusLEDTest, AuthFailurePattern_Error3PulsePlus2TinyPlusSeparator) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::AUTH_FAILURE);
    led.update(0);

    // ERROR_3_PULSE: 3× (SHORT_FLASH ON, SHORT_GAP OFF)
    // First SHORT_FLASH (200ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "First SHORT_FLASH ON";
    mock.advanceTime(200, led);

    // First SHORT_GAP (200ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "First SHORT_GAP OFF";
    mock.advanceTime(200, led);

    // Second SHORT_FLASH ON
    EXPECT_TRUE(mock.getCurrentState()) << "Second SHORT_FLASH ON";
    mock.advanceTime(200, led);

    // Second SHORT_GAP OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Second SHORT_GAP OFF";
    mock.advanceTime(200, led);

    // Third SHORT_FLASH ON
    EXPECT_TRUE(mock.getCurrentState()) << "Third SHORT_FLASH ON";
    mock.advanceTime(200, led);

    // Third SHORT_GAP OFF (end of ERROR_3_PULSE)
    EXPECT_FALSE(mock.getCurrentState()) << "Third SHORT_GAP OFF";
    mock.advanceTime(200, led);

    // 2×TINY_PULSE
    // First TINY_FLASH (100ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "First TINY_FLASH ON";
    mock.advanceTime(100, led);

    // First TINY_GAP (100ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "First TINY_GAP OFF";
    mock.advanceTime(100, led);

    // Second TINY_FLASH (100ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Second TINY_FLASH ON";
    mock.advanceTime(100, led);

    // Second TINY_GAP (100ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Second TINY_GAP OFF";
    mock.advanceTime(100, led);

    // SEPARATOR (2000ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in SEPARATOR";
}

// ── TEST: ERROR_RECOVERABLE Pattern (ERROR_3_PULSE + 3×TINY_PULSE + SEPARATOR) ───────

TEST(StatusLEDTest, ErrorRecoverablePattern_Error3PulsePlus3TinyPlusSeparator) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::ERROR_RECOVERABLE);
    led.update(0);

    // Skip through ERROR_3_PULSE (6 steps, 1200ms total)
    for (int i = 0; i < 3; i++) {
        mock.advanceTime(200, led);  // SHORT_FLASH ON
        mock.advanceTime(200, led);  // SHORT_GAP OFF
    }

    // 3×TINY_PULSE
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(mock.getCurrentState()) << "TINY_FLASH " << (i+1) << " ON";
        mock.advanceTime(100, led);
        EXPECT_FALSE(mock.getCurrentState()) << "TINY_GAP " << (i+1) << " OFF";
        mock.advanceTime(100, led);
    }

    // SEPARATOR (2000ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in SEPARATOR";
}

// ── TEST: ERROR_NO_NTP_SERVICE Pattern (ERROR_3_PULSE + 1×TINY_PULSE + SEPARATOR) ───

TEST(StatusLEDTest, ErrorNoNtpServicePattern_Error3PulsePlus1TinyPlusSeparator) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    led.update(0);

    // Skip through ERROR_3_PULSE (6 steps, 1200ms total)
    for (int i = 0; i < 3; i++) {
        mock.advanceTime(200, led);  // SHORT_FLASH ON
        mock.advanceTime(200, led);  // SHORT_GAP OFF
    }

    // 1×TINY_PULSE
    EXPECT_TRUE(mock.getCurrentState()) << "TINY_FLASH ON";
    mock.advanceTime(100, led);
    EXPECT_FALSE(mock.getCurrentState()) << "TINY_GAP OFF";
    mock.advanceTime(100, led);

    // SEPARATOR (2000ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in SEPARATOR";
}

// ── TEST: FATAL_UNRECOVERABLE SOS Pattern (3 short, 3 long, 3 short, SEPARATOR) ────

TEST(StatusLEDTest, FatalUnrecoverablePattern_SOS) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::FATAL_UNRECOVERABLE);
    led.update(0);

    // 3 short (SHORT_FLASH 200ms ON, SHORT_GAP 200ms OFF)
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(mock.getCurrentState()) << "Short " << (i+1) << " ON";
        mock.advanceTime(200, led);
        EXPECT_FALSE(mock.getCurrentState()) << "Short " << (i+1) << " OFF";
        mock.advanceTime(200, led);
    }

    // 3 long (LONG_FLASH 800ms ON, SHORT_GAP 200ms OFF)
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(mock.getCurrentState()) << "Long " << (i+1) << " ON";
        mock.advanceTime(800, led);
        EXPECT_FALSE(mock.getCurrentState()) << "Long " << (i+1) << " OFF";
        mock.advanceTime(200, led);
    }

    // 3 short again
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(mock.getCurrentState()) << "Second short " << (i+1) << " ON";
        mock.advanceTime(200, led);
        EXPECT_FALSE(mock.getCurrentState()) << "Second short " << (i+1) << " OFF";
        mock.advanceTime(200, led);
    }

    // SEPARATOR (2000ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF in SEPARATOR";

    // After SEPARATOR, should cycle back to first short ON
    mock.advanceTime(2000, led);
    EXPECT_TRUE(mock.getCurrentState()) << "Should cycle back to first short ON";
}

// ── TEST: OFF Pattern (SOLID OFF) ─────────────────────────────────────────────────────

TEST(StatusLEDTest, OffPattern_SolidOff) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::OFF);
    led.update(0);

    // Should be OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Should be solid OFF";

    // Should stay OFF regardless of time passing
    mock.advanceTime(1000, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should stay OFF after 1s";

    mock.advanceTime(5000, led);
    EXPECT_FALSE(mock.getCurrentState()) << "Should stay OFF after 6s";

    // Critical test: uniform cycling with single OFF state - stays OFF forever
    mock.advanceTime(86400001, led);  // Advance past 24 hours
    EXPECT_FALSE(mock.getCurrentState()) << "Should stay OFF after 24+ hours (cycling but always OFF)";

    // Should stay off indefinitely through many cycles
    mock.advanceTime(100000000, led);  // Advance additional ~27 hours
    EXPECT_FALSE(mock.getCurrentState()) << "Should stay OFF indefinitely (cycling but always OFF)";
}

// ── TEST: All Error Patterns Start With ERROR_3_PULSE ────────────────────────────────

TEST(StatusLEDTest, AllErrorPatternsStartWithError3Pulse) {
    // All error patterns must start with 3 short pulses (ERROR_3_PULSE)
    // This is a key business rule for pattern distinguishability

    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    std::vector<firmware::StatusLED::Pattern> errorPatterns = {
        firmware::StatusLED::Pattern::AUTH_FAILURE,
        firmware::StatusLED::Pattern::ERROR_RECOVERABLE,
        firmware::StatusLED::Pattern::ERROR_NO_NTP_SERVICE,
        firmware::StatusLED::Pattern::FATAL_UNRECOVERABLE
    };

    for (auto pattern : errorPatterns) {
        mock.reset();
        led.init();
        mock.currentTime = 0;

        led.setPattern(pattern);
        led.update(0);

        // Verify ERROR_3_PULSE: 3× (SHORT_FLASH ON, SHORT_GAP OFF)
        for (int i = 0; i < 3; i++) {
            // Each short pulse starts with ON
            EXPECT_TRUE(mock.getCurrentState())
                << "Pattern " << static_cast<int>(pattern) << " pulse " << (i+1) << " should start ON";

            // Advance through SHORT_FLASH (200ms)
            mock.advanceTime(200, led);

            // Then goes to SHORT_GAP (200ms)
            EXPECT_FALSE(mock.getCurrentState())
                << "Pattern " << static_cast<int>(pattern) << " pulse " << (i+1) << " should be in GAP";

            // Advance through SHORT_GAP
            mock.advanceTime(200, led);
        }
    }
}

// ── TEST: Pattern Distinguishability ────────────────────────────────────────────────

TEST(StatusLEDTest, ErrorPatternsDistinguishableByLength) {
    // Error patterns should be distinguishable by their total length after ERROR_3_PULSE
    // AUTH_FAILURE: 6 + 4 + 1 = 11 steps
    // ERROR_RECOVERABLE: 6 + 6 + 1 = 13 steps
    // ERROR_NO_NTP_SERVICE: 6 + 2 + 1 = 9 steps
    // FATAL_UNRECOVERABLE: 6 + 6 + 6 + 1 = 19 steps

    using namespace firmware;

    auto [authSteps, authCount] = StatusLED::getPatternSteps(StatusLED::Pattern::AUTH_FAILURE);
    auto [recovSteps, recovCount] = StatusLED::getPatternSteps(StatusLED::Pattern::ERROR_RECOVERABLE);
    auto [ntpSteps, ntpCount] = StatusLED::getPatternSteps(StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    auto [fatalSteps, fatalCount] = StatusLED::getPatternSteps(StatusLED::Pattern::FATAL_UNRECOVERABLE);

    EXPECT_EQ(authCount, 11) << "AUTH_FAILURE should have 11 steps";
    EXPECT_EQ(recovCount, 13) << "ERROR_RECOVERABLE should have 13 steps";
    EXPECT_EQ(ntpCount, 9) << "ERROR_NO_NTP_SERVICE should have 9 steps";
    EXPECT_EQ(fatalCount, 19) << "FATAL_UNRECOVERABLE should have 19 steps";

    // All should be different
    std::vector<size_t> counts = {authCount, recovCount, ntpCount, fatalCount};
    std::sort(counts.begin(), counts.end());
    for (size_t i = 1; i < counts.size(); i++) {
        EXPECT_NE(counts[i-1], counts[i]) << "Pattern counts should be unique";
    }
}

// ── TEST: Pattern Switching ────────────────────────────────────────────────────────

TEST(StatusLEDTest, PatternSwitching_MidCycle) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    // Start with WIFI_SEARCHING
    led.setPattern(firmware::StatusLED::Pattern::WIFI_SEARCHING);
    led.update(0);

    // Mid-cycle (50ms into 100ms ON period)
    mock.advanceTime(50, led);
    EXPECT_TRUE(mock.getCurrentState());

    // Switch to CLIENT_CONNECTED (solid on)
    led.setPattern(firmware::StatusLED::Pattern::CLIENT_CONNECTED);
    led.update(50);
    EXPECT_TRUE(mock.getCurrentState());

    // Should stay on
    mock.advanceTime(1000, led);
    EXPECT_TRUE(mock.getCurrentState());
}

TEST(StatusLEDTest, PatternSwitching_OffToOn) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    // Start OFF
    led.setPattern(firmware::StatusLED::Pattern::OFF);
    led.update(0);
    EXPECT_FALSE(mock.getCurrentState());

    // Switch to CLIENT_CONNECTED
    led.setPattern(firmware::StatusLED::Pattern::CLIENT_CONNECTED);
    led.update(100);
    EXPECT_TRUE(mock.getCurrentState());
}

// ── TEST: Immediate Pattern Interruption ────────────────────────────────────────────────

TEST(StatusLEDTest, PatternSwitching_ImmediateInterruption) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    // Start with a pattern that has long steps (FATAL_UNRECOVERABLE SOS pattern)
    led.setPattern(firmware::StatusLED::Pattern::FATAL_UNRECOVERABLE);
    led.update(0);

    // Advance partway through the first step (but not complete it)
    // First step of SOS is SHORT_FLASH (200ms) ON
    mock.advanceTime(50, led);  // Only 50ms into the 200ms step
    EXPECT_TRUE(mock.getCurrentState()) << "Should be ON in first short pulse";

    // CRITICAL TEST: Switch to a different pattern mid-step
    // This should interrupt the current 200ms step immediately
    led.setPattern(firmware::StatusLED::Pattern::WIFI_CONNECTED);

    // Advance just 1 tick (1ms) - the new pattern should start immediately
    mock.advanceTime(1, led);

    // Verify we're now in the WIFI_CONNECTED pattern's first step (LONG_FLASH ON, 800ms)
    // NOT continuing the SOS pattern's first step (SHORT_FLASH)
    EXPECT_TRUE(mock.getCurrentState()) << "Should be ON (new pattern's first step)";

    // Advance through what would have been the remainder of the SOS step
    // (200ms - 50ms = 150ms more)
    mock.advanceTime(150, led);

    // If we were still in SOS pattern, we'd now be in the SHORT_GAP (200ms) OFF
    // But since we switched, we should still be in WIFI_CONNECTED's LONG_FLASH (800ms) ON
    EXPECT_TRUE(mock.getCurrentState()) << "Should still be ON (WIFI_CONNECTED LONG_FLASH)";

    // Complete the WIFI_CONNECTED LONG_FLASH (800ms total, we've done 150ms so far)
    mock.advanceTime(650, led);  // 800ms total - 150ms done = 650ms more

    // Now we should be in WIFI_CONNECTED's SHORT_GAP (200ms) OFF
    EXPECT_FALSE(mock.getCurrentState()) << "Should be OFF (WIFI_CONNECTED SHORT_GAP)";
}

// ── TEST: Non-Blocking Behavior ──────────────────────────────────────────────────────

TEST(StatusLEDTest, NonBlocking_MultipleUpdatesSameTimestamp) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::WIFI_SEARCHING);
    led.update(0);

    size_t transitionCount = mock.getTransitionCount();

    // Multiple updates at same timestamp should not change state
    led.update(0);
    led.update(0);
    led.update(0);

    EXPECT_EQ(mock.getTransitionCount(), transitionCount);
}

// ── TEST: Pattern Cycling ────────────────────────────────────────────────────────────

TEST(StatusLEDTest, PatternCyclesContinuously) {
    MockStatusLEDOutput mock;
    firmware::StatusLED led(&mock);

    led.init();
    mock.currentTime = 0;

    led.setPattern(firmware::StatusLED::Pattern::BOOT);
    led.update(0);

    // BOOT pattern: MED_FLASH (500ms) ON, MED_GAP (500ms) OFF = 1000ms cycle
    for (int cycle = 0; cycle < 3; cycle++) {
        EXPECT_TRUE(mock.getCurrentState()) << "Cycle " << cycle << " should start ON";
        mock.advanceTime(500, led);
        EXPECT_FALSE(mock.getCurrentState()) << "Cycle " << cycle << " should be OFF";
        mock.advanceTime(500, led);
    }
}
