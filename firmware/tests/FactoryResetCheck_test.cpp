// FactoryResetCheck_test.cpp - Host tests for FactoryResetCheck
// Extracted from can-bridge.ino for host testability

#include "FactoryResetCheck.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>

using esp32_firmware::FactoryResetCheck;
using esp32_firmware::IFactoryResetGpio;
using esp32_firmware::IFactoryResetDelay;
using esp32_firmware::IFactoryResetLogger;
using esp32_firmware::ICredentialClear;

namespace {

// Scripted GPIO: returns a predefined sequence of pressed/released states,
// then sticks at the last value. isPressed() advances through the script.
class FakeGpio : public IFactoryResetGpio {
public:
    explicit FakeGpio(std::vector<bool> states) : states_(std::move(states)) {}

    bool isPressed() override {
        if (pos_ < states_.size()) {
            return states_[pos_++];
        }
        // Stick at last value once the script is exhausted.
        return states_.empty() ? false : states_.back();
    }

    void reset(const std::vector<bool>& states) {
        states_ = states;
        pos_ = 0;
    }

private:
    std::vector<bool> states_;
    std::size_t pos_ = 0;
};

// Recording delay: captures the requested delay durations for test assertions.
class FakeDelay : public IFactoryResetDelay {
public:
    std::vector<uint32_t> delays;

    void delayMs(uint32_t ms) override {
        delays.push_back(ms);
    }

    void reset() { delays.clear(); }
};

// Recording logger: captures log messages with severity for test assertions.
class FakeLogger : public IFactoryResetLogger {
public:
    struct Entry {
        std::string msg;
        bool isConfirmed;
    };
    std::vector<Entry> entries;

    void log(const char* msg, bool isConfirmed) override {
        entries.push_back({msg, isConfirmed});
    }

    void reset() { entries.clear(); }
};

// Recording credential clearer: tracks whether clear() was called.
class FakeCredClear : public ICredentialClear {
public:
    bool cleared = false;

    void clear() override { cleared = true; }

    void reset() { cleared = false; }
};

} // namespace

// ── Not-pressed path ──────────────────────────────────────────────────────────

// If the pin is not pressed initially, run() returns false immediately without
// entering the debounce loop or clearing credentials.
TEST(FactoryResetCheckTest, NotPressedInitiallyReturnsFalse) {
    FakeGpio gpio{{false}};       // not pressed from the start
    FakeDelay delay;
    FakeLogger logger;
    FakeCredClear credClear;

    FactoryResetCheck checker(3000, 100, gpio, delay, logger, credClear);
    EXPECT_FALSE(checker.run());

    // No debounce activity: delay was never called.
    EXPECT_TRUE(delay.delays.empty());
    // No credential clear.
    EXPECT_FALSE(credClear.cleared);
    // No log messages (the initial check is silent, matching the .ino).
    EXPECT_TRUE(logger.entries.empty());
}

// ── Confirmed path ────────────────────────────────────────────────────────────

// If the pin is held for the full threshold, run() returns true and clears
// credentials. The debounce loop accumulates held time across multiple polls.
TEST(FactoryResetCheckTest, HeldForThresholdConfirmsAndClears) {
    // 300ms threshold, 100ms poll: need 3 consecutive "pressed" reads.
    // Initial check consumes 1; vector provides 3 (sticky-back gives the 3rd loop read).
    FakeGpio gpio{{true, true, true}};
    FakeDelay delay;
    FakeLogger logger;
    FakeCredClear credClear;

    FactoryResetCheck checker(300, 100, gpio, delay, logger, credClear);
    EXPECT_TRUE(checker.run());

    // Debouncer: tick1=WAITING, tick2=WAITING, tick3=CONFIRMED.
    // Delay called after tick1 and tick2 (tick3 breaks before delay).
    ASSERT_EQ(delay.delays.size(), 2u);
    EXPECT_EQ(delay.delays[0], 100u);
    EXPECT_EQ(delay.delays[1], 100u);

    // Credentials were cleared.
    EXPECT_TRUE(credClear.cleared);

    // Two log entries: loop-entry (YELLOW) + confirmed (RED).
    ASSERT_EQ(logger.entries.size(), 2u);
    EXPECT_EQ(logger.entries[0].msg, "Factory reset: GPIO0 held at boot, waiting 300ms to confirm...");
    EXPECT_FALSE(logger.entries[0].isConfirmed);
    EXPECT_EQ(logger.entries[1].msg, "Factory reset: clearing WiFi credentials and booting to AP mode");
    EXPECT_TRUE(logger.entries[1].isConfirmed);
}

// ── Cancelled path ────────────────────────────────────────────────────────────

// If the pin is released before the threshold, run() returns false and does
// not clear credentials.
TEST(FactoryResetCheckTest, ReleasedBeforeThresholdCancels) {
    // 300ms threshold, 100ms poll: pressed for 2 ticks (200ms), then released.
    // The initial isPressed() check consumes the first element, so the loop
    // receives {true, true, false} → 2 delays then CANCELLED.
    FakeGpio gpio{{true, true, true, false}};
    FakeDelay delay;
    FakeLogger logger;
    FakeCredClear credClear;

    FactoryResetCheck checker(300, 100, gpio, delay, logger, credClear);
    EXPECT_FALSE(checker.run());

    // Debouncer: tick1=WAITING, tick2=WAITING, tick3=CANCELLED.
    // Delay called after tick1 and tick2 (tick3 breaks before delay).
    ASSERT_EQ(delay.delays.size(), 2u);
    EXPECT_EQ(delay.delays[0], 100u);
    EXPECT_EQ(delay.delays[1], 100u);

    // Credentials NOT cleared.
    EXPECT_FALSE(credClear.cleared);

    // Two log entries: loop-entry (YELLOW) + cancelled (YELLOW).
    ASSERT_EQ(logger.entries.size(), 2u);
    EXPECT_EQ(logger.entries[0].msg, "Factory reset: GPIO0 held at boot, waiting 300ms to confirm...");
    EXPECT_FALSE(logger.entries[0].isConfirmed);
    EXPECT_EQ(logger.entries[1].msg, "Factory reset: released early, cancelling");
    EXPECT_FALSE(logger.entries[1].isConfirmed);
}

// ── Zero-threshold edge case ──────────────────────────────────────────────────

// A zero hold threshold confirms on the first poll tick (heldMs = pollMs >= 0).
TEST(FactoryResetCheckTest, ZeroThresholdConfirmsOnFirstTick) {
    FakeGpio gpio{{true}};       // pressed once
    FakeDelay delay;
    FakeLogger logger;
    FakeCredClear credClear;

    FactoryResetCheck checker(0, 100, gpio, delay, logger, credClear);
    EXPECT_TRUE(checker.run());

    // No delay calls: the debouncer confirms on the first feed, so the loop
    // breaks before any delay.
    EXPECT_TRUE(delay.delays.empty());
    EXPECT_TRUE(credClear.cleared);

    // Two log entries: loop-entry + confirmed.
    ASSERT_EQ(logger.entries.size(), 2u);
    EXPECT_EQ(logger.entries[0].msg, "Factory reset: GPIO0 held at boot, waiting 0ms to confirm...");
    EXPECT_FALSE(logger.entries[0].isConfirmed);
    EXPECT_EQ(logger.entries[1].msg, "Factory reset: clearing WiFi credentials and booting to AP mode");
    EXPECT_TRUE(logger.entries[1].isConfirmed);
}

// ── Poll interval is respected ────────────────────────────────────────────────

// The delay between polls matches the configured poll interval.
TEST(FactoryResetCheckTest, DelayMatchesPollInterval) {
    // 500ms threshold, 150ms poll: 4 pressed readings needed.
    FakeGpio gpio{{true, true, true, true}};
    FakeDelay delay;
    FakeLogger logger;
    FakeCredClear credClear;

    FactoryResetCheck checker(500, 150, gpio, delay, logger, credClear);
    EXPECT_TRUE(checker.run());

    // Three delays of 150ms (after ticks 1, 2, 3; tick 4 breaks before delay).
    ASSERT_EQ(delay.delays.size(), 3u);
    EXPECT_EQ(delay.delays[0], 150u);
    EXPECT_EQ(delay.delays[1], 150u);
    EXPECT_EQ(delay.delays[2], 150u);
    EXPECT_TRUE(credClear.cleared);

    // Two log entries: loop-entry + confirmed.
    ASSERT_EQ(logger.entries.size(), 2u);
    EXPECT_EQ(logger.entries[0].msg, "Factory reset: GPIO0 held at boot, waiting 500ms to confirm...");
    EXPECT_FALSE(logger.entries[0].isConfirmed);
    EXPECT_EQ(logger.entries[1].msg, "Factory reset: clearing WiFi credentials and booting to AP mode");
    EXPECT_TRUE(logger.entries[1].isConfirmed);
}
