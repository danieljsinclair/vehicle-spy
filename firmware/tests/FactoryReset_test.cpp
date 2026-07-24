// FactoryReset_test.cpp - Host tests for FactoryResetDebouncer
// Extracted from can-bridge.ino for host testability

#include "FactoryReset.h"

#include <gtest/gtest.h>
#include <cstdint>

using esp32_firmware::FactoryResetDebouncer;
using esp32_firmware::FactoryResetResult;

// ── IDLE state ──────────────────────────────────────────────────────────────

// A released pin in IDLE stays undecided (no cancellation until a press
// has been registered — the debouncer is only created in the .ino after the
// initial GPIO read shows LOW, so IDLE+false is unreachable in production,
// but the state machine handles it gracefully).
TEST(FactoryResetTest, IdleReleasedStaysUndecided) {
    FactoryResetDebouncer d(3000, 100);
    EXPECT_FALSE(d.isConfirmed());
    EXPECT_FALSE(d.isCancelled());
    EXPECT_EQ(d.feed(false), FactoryResetResult::WAITING);
}

// A single press in IDLE transitions to HOLDING but doesn't confirm yet
// (heldMs starts at pollIntervalMs, which is below a non-trivial threshold).
TEST(FactoryResetTest, IdlePressedTransitionsToHolding) {
    FactoryResetDebouncer d(3000, 100);
    auto r = d.feed(true);
    EXPECT_EQ(r, FactoryResetResult::WAITING);
    EXPECT_FALSE(d.isConfirmed());
    EXPECT_FALSE(d.isCancelled());
}

// ── HOLDING state ───────────────────────────────────────────────────────────

// Continuous presses accumulate held time; confirm only after threshold.
TEST(FactoryResetTest, ContinuousPressesAccumulateAndConfirm) {
    FactoryResetDebouncer d(300, 100);  // 300ms threshold, 100ms poll = 3 ticks
    // Tick 1: heldMs=100, not yet
    EXPECT_EQ(d.feed(true), FactoryResetResult::WAITING);
    // Tick 2: heldMs=200, not yet
    EXPECT_EQ(d.feed(true), FactoryResetResult::WAITING);
    // Tick 3: heldMs=300, confirmed
    EXPECT_EQ(d.feed(true), FactoryResetResult::CONFIRMED);
    EXPECT_EQ(d.feed(true), FactoryResetResult::CONFIRMED);  // terminal
    EXPECT_TRUE(d.isConfirmed());
}

// Early release cancels the reset.
TEST(FactoryResetTest, EarlyReleaseCancels) {
    FactoryResetDebouncer d(3000, 100);
    EXPECT_EQ(d.feed(true), FactoryResetResult::WAITING);  // tick 1: holding
    EXPECT_EQ(d.feed(true), FactoryResetResult::WAITING);  // tick 2: holding
    EXPECT_EQ(d.feed(false), FactoryResetResult::CANCELLED); // tick 3: released
    EXPECT_TRUE(d.isCancelled());
}

// Once cancelled, further feeds stay cancelled (terminal state).
TEST(FactoryResetTest, CancelledIsTerminal) {
    FactoryResetDebouncer d(3000, 100);
    // First feed: IDLE→HOLDING, still waiting
    EXPECT_EQ(d.feed(true), FactoryResetResult::WAITING);
    // Second feed: released → cancelled
    EXPECT_EQ(d.feed(false), FactoryResetResult::CANCELLED);
    EXPECT_TRUE(d.isCancelled());
    // Terminal: further feeds still return CANCELLED
    EXPECT_EQ(d.feed(true), FactoryResetResult::CANCELLED);
    EXPECT_EQ(d.feed(false), FactoryResetResult::CANCELLED);
}

// ── CONFIRMED state ─────────────────────────────────────────────────────────

// Once confirmed, further feeds stay confirmed (terminal state).
TEST(FactoryResetTest, ConfirmedIsTerminal) {
    FactoryResetDebouncer d(100, 100);  // 1 tick threshold
    // First feed: IDLE→HOLDING, heldMs=100 >= 100 → CONFIRMED
    EXPECT_EQ(d.feed(true), FactoryResetResult::CONFIRMED);
    EXPECT_EQ(d.feed(true), FactoryResetResult::CONFIRMED);  // terminal
    EXPECT_EQ(d.feed(false), FactoryResetResult::CONFIRMED); // even release
    EXPECT_TRUE(d.isConfirmed());
}

// ── Edge cases ──────────────────────────────────────────────────────────────

// Zero threshold: confirms on first press (heldMs=pollInterval >= 0).
TEST(FactoryResetTest, ZeroThresholdConfirmsImmediately) {
    FactoryResetDebouncer d(0, 100);
    EXPECT_EQ(d.feed(true), FactoryResetResult::CONFIRMED);
    EXPECT_TRUE(d.isConfirmed());
}

// Threshold equal to poll interval: confirms on first press (heldMs=pollInterval >= threshold).
TEST(FactoryResetTest, ThresholdEqualPollIntervalConfirmsImmediately) {
    FactoryResetDebouncer d(100, 100);
    EXPECT_EQ(d.feed(true), FactoryResetResult::CONFIRMED);
    EXPECT_TRUE(d.isConfirmed());
}

// Released before first press: stays idle (not cancelled, not confirmed).
// This mirrors the .ino behavior where the debouncer is only created after
// the initial GPIO read shows LOW — a release at that point means "no reset."
TEST(FactoryResetTest, ReleasedBeforeFirstPressStaysIdle) {
    FactoryResetDebouncer d(3000, 100);
    EXPECT_EQ(d.feed(false), FactoryResetResult::WAITING);
    EXPECT_FALSE(d.isCancelled());
    EXPECT_FALSE(d.isConfirmed());
}
