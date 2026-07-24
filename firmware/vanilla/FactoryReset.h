#pragma once

// FactoryReset.h - Factory reset debounce state machine
// Extracted from can-bridge.ino for host testability
//
// The .ino owns the GPIO read (hardware); this class owns the debounce/threshold
// logic. The .ino feeds it live pin readings and acts on the result.
//
// C++14 compatible: std::optional is NOT used (ESP32 toolchain is gnu++14).
// Use FactoryResetResult enum instead.
//
// State machine:
//   IDLE      → first pressed → HOLDING
//   HOLDING   → released early → CANCELLED
//   HOLDING   → held for full threshold → CONFIRMED
//   CONFIRMED → terminal: always returns CONFIRMED
//   CANCELLED → terminal: always returns CANCELLED
//
// IDLE + not-pressed stays WAITING — cancellation only triggers after a press
// has been registered, matching the original .ino behavior where the debounce
// loop is entered only after the initial GPIO read shows LOW.

#include <cstdint>

namespace esp32_firmware {

// Result of a debounce poll tick.
enum class FactoryResetResult {
    WAITING,   // still polling, no result yet
    CONFIRMED, // held for full duration → trigger reset
    CANCELLED  // released early → no reset
};

// Factory reset debounce state machine — host-testable.
// The .ino drives it with live GPIO readings; vanilla owns the threshold logic.
class FactoryResetDebouncer {
public:
    explicit FactoryResetDebouncer(uint32_t holdMs, uint32_t pollIntervalMs = 100);

    // Feed a pin reading (true = pressed/low, false = released/high).
    // Returns WAITING while still polling, CONFIRMED or CANCELLED once decided.
    FactoryResetResult feed(bool isPressed);

    // Has the reset been confirmed?
    bool isConfirmed() const { return state_ == State::CONFIRMED; }

    // Has the reset been cancelled?
    bool isCancelled() const { return state_ == State::CANCELLED; }

private:
    enum class State { IDLE, HOLDING, CONFIRMED, CANCELLED };

    uint32_t holdMs_;
    uint32_t pollIntervalMs_;
    uint32_t heldMs_ = 0;
    State state_ = State::IDLE;
};

} // namespace esp32_firmware
