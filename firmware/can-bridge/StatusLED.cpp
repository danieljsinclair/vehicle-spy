#include "StatusLED.h"
#include "IStatusLEDOutput.h"

namespace firmware {

// ── Declarative Pattern Table ────────────────────────────────────────────────────
// Each pattern is an array of opcodes: {state, duration_ms}
// StatusLED steps through the array non-blocking, cycling on completion

// BOOT: MED_FLASH ON, MED_GAP OFF (cycles continuously)
static constexpr LEDStep PATTERN_BOOT[] = {
    {LEDState::ON,  StatusLEDConstants::MED_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::MED_GAP_MS}
};

// WIFI_SEARCHING: TINY_FLASH ON, SEARCHING_GAP OFF (0.1s on, 0.9s off)
static constexpr LEDStep PATTERN_WIFI_SEARCHING[] = {
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SEARCHING_GAP_MS}
};

// WIFI_CONNECTED: LONG_FLASH ON, SHORT_GAP OFF (0.8s on, 0.2s off)
static constexpr LEDStep PATTERN_WIFI_CONNECTED[] = {
    {LEDState::ON,  StatusLEDConstants::LONG_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS}
};

// CLIENT_CONNECTED: SOLID ON (single-state pattern - cycles but state never changes)
static constexpr LEDStep PATTERN_CLIENT_CONNECTED[] = {
    // DESIGN NOTE: Could be any duration as we switch patterns immediately. If we decide to
    // finish cycles, don't make it longer than SEPARATOR. As we switch immediately, a longer
    // duration saves wasting cycles.
    {LEDState::ON, StatusLEDConstants::SEPARATOR_MS}  // 2s - cycles but stays ON forever
};

// AP_MODE: LONG_FLASH ON, TINY_GAP OFF, TINY_FLASH ON, TINY_GAP OFF, TINY_FLASH ON, SEPARATOR OFF
static constexpr LEDStep PATTERN_AP_MODE[] = {
    {LEDState::ON,  StatusLEDConstants::LONG_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}
};

// OTA_IN_PROGRESS: SHORT_FLASH ON, SHORT_GAP OFF (0.2s on/off rapid)
static constexpr LEDStep PATTERN_OTA_IN_PROGRESS[] = {
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS}
};

// AUTH_FAILURE: ERROR_3_PULSE + 2×TINY_PULSE + SEPARATOR
static constexpr LEDStep PATTERN_AUTH_FAILURE[] = {
    // ERROR_3_PULSE (6 steps)
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    // 2×TINY_PULSE (4 steps)
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    // SEPARATOR
    {LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}
};

// ERROR_RECOVERABLE: ERROR_3_PULSE + 3×TINY_PULSE + SEPARATOR
static constexpr LEDStep PATTERN_ERROR_RECOVERABLE[] = {
    // ERROR_3_PULSE (6 steps)
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    // 3×TINY_PULSE (6 steps)
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    // SEPARATOR
    {LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}
};

// ERROR_NO_NTP_SERVICE: ERROR_3_PULSE + 1×TINY_PULSE + SEPARATOR
static constexpr LEDStep PATTERN_ERROR_NO_NTP_SERVICE[] = {
    // ERROR_3_PULSE (6 steps)
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    // 1×TINY_PULSE (2 steps)
    {LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    // SEPARATOR
    {LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}
};

// FATAL_UNRECOVERABLE: SOS - 3 short, 3 long, 3 short, SEPARATOR
static constexpr LEDStep PATTERN_FATAL_UNRECOVERABLE[] = {
    // 3 short (6 steps)
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    // 3 long (6 steps)
    {LEDState::ON,  StatusLEDConstants::LONG_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::LONG_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::LONG_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    // 3 short (6 steps)
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    // SEPARATOR
    {LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}
};

// OFF: SOLID OFF (single-state pattern - cycles but state never changes)
static constexpr LEDStep PATTERN_OFF[] = {
    // DESIGN NOTE: Could be any duration as we switch patterns immediately. If we decide to
    // finish cycles, don't make it longer than SEPARATOR. As we switch immediately, a longer
    // duration saves wasting cycles.
    {LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}  // 2s - cycles but stays OFF forever
};

// ── Pattern Lookup Table ───────────────────────────────────────────────────────────
std::pair<const LEDStep*, size_t> StatusLED::getPatternSteps(Pattern pattern) {
    switch (pattern) {
        case Pattern::BOOT:
            return {PATTERN_BOOT, sizeof(PATTERN_BOOT) / sizeof(LEDStep)};
        case Pattern::WIFI_SEARCHING:
            return {PATTERN_WIFI_SEARCHING, sizeof(PATTERN_WIFI_SEARCHING) / sizeof(LEDStep)};
        case Pattern::WIFI_CONNECTED:
            return {PATTERN_WIFI_CONNECTED, sizeof(PATTERN_WIFI_CONNECTED) / sizeof(LEDStep)};
        case Pattern::CLIENT_CONNECTED:
            return {PATTERN_CLIENT_CONNECTED, sizeof(PATTERN_CLIENT_CONNECTED) / sizeof(LEDStep)};
        case Pattern::AP_MODE:
            return {PATTERN_AP_MODE, sizeof(PATTERN_AP_MODE) / sizeof(LEDStep)};
        case Pattern::OTA_IN_PROGRESS:
            return {PATTERN_OTA_IN_PROGRESS, sizeof(PATTERN_OTA_IN_PROGRESS) / sizeof(LEDStep)};
        case Pattern::AUTH_FAILURE:
            return {PATTERN_AUTH_FAILURE, sizeof(PATTERN_AUTH_FAILURE) / sizeof(LEDStep)};
        case Pattern::ERROR_RECOVERABLE:
            return {PATTERN_ERROR_RECOVERABLE, sizeof(PATTERN_ERROR_RECOVERABLE) / sizeof(LEDStep)};
        case Pattern::ERROR_NO_NTP_SERVICE:
            return {PATTERN_ERROR_NO_NTP_SERVICE, sizeof(PATTERN_ERROR_NO_NTP_SERVICE) / sizeof(LEDStep)};
        case Pattern::FATAL_UNRECOVERABLE:
            return {PATTERN_FATAL_UNRECOVERABLE, sizeof(PATTERN_FATAL_UNRECOVERABLE) / sizeof(LEDStep)};
        case Pattern::OFF:
        default:
            return {PATTERN_OFF, sizeof(PATTERN_OFF) / sizeof(LEDStep)};
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────────
StatusLED::StatusLED(IStatusLEDOutput* output)
    : output_(output)
    , currentPattern_(Pattern::OFF)
    , lastPattern_(Pattern::OFF)
    , lastUpdateTime_(0)
    , phaseStartTime_(0)
    , stepIndex_(0)
    , ledOn_(false) {
}

// ── Initialize ───────────────────────────────────────────────────────────────────
void StatusLED::init() {
    if (output_) {
        output_->init();
    }
    // Turn LED OFF first (user requirement: "Don't forget to turn off the blue LED on reboot")
    setLedOn(false);
    // Set BOOT pattern for startup indication
    setPattern(Pattern::BOOT);
}

// ── Set Pattern ───────────────────────────────────────────────────────────────────
void StatusLED::setPattern(Pattern pattern) {
    currentPattern_ = pattern;
    // Pattern will reset on next update when change is detected
}

// ── Update (called from loop) ────────────────────────────────────────────────────
void StatusLED::update(uint32_t currentTime) {
    // DESIGN NOTE: interrupts the current pattern immediately — does NOT finish the
    // current cycle. This is required because OFF/ON patterns use long durations
    // (up to 1h+). If changed to finish-cycle behaviour, ALL pattern durations must
    // be ≤2s (reduce OFF/ON durations accordingly).
    // Detect pattern change and reset to beginning
    if (currentPattern_ != lastPattern_) {
        resetPattern(currentTime);
        lastPattern_ = currentPattern_;
    }

    lastUpdateTime_ = currentTime;

    // Get current pattern steps
    auto [steps, stepCount] = getPatternSteps(currentPattern_);
    if (stepCount == 0) return;

    // Get current step
    const LEDStep& currentStep = steps[stepIndex_];
    uint32_t elapsed = currentTime - phaseStartTime_;

    // Check if we've completed this step
    if (elapsed >= currentStep.durationMs) {
        // Move to next step
        stepIndex_++;
        phaseStartTime_ = currentTime;

        // Check if pattern is complete
        if (stepIndex_ >= stepCount) {
            // Cycle back to beginning
            stepIndex_ = 0;
        }

        // Apply new step's state immediately
        const LEDStep& nextStep = steps[stepIndex_];
        bool newState = (nextStep.state == LEDState::ON);
        setLedOn(newState);
    } else {
        // Still in current step - apply state
        bool shouldBeOn = (currentStep.state == LEDState::ON);
        setLedOn(shouldBeOn);
    }
}

// ── Reset Pattern ────────────────────────────────────────────────────────────────
void StatusLED::resetPattern(uint32_t currentTime) {
    stepIndex_ = 0;
    phaseStartTime_ = currentTime;

    // Apply first step's state immediately
    auto [steps, stepCount] = getPatternSteps(currentPattern_);
    if (stepCount > 0) {
        bool newState = (steps[0].state == LEDState::ON);
        setLedOn(newState);
    }
}

// ── Set LED State ─────────────────────────────────────────────────────────────────
void StatusLED::setLedOn(bool on) {
    if (ledOn_ != on) {
        ledOn_ = on;
        if (output_) {
            output_->setOn(on);
        }
    }
}

} // namespace firmware
