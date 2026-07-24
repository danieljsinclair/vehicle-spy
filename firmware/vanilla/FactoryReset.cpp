// FactoryReset.cpp - Factory reset debounce state machine
// Extracted from can-bridge.ino for host testability

#include "FactoryReset.h"

namespace esp32_firmware {

FactoryResetDebouncer::FactoryResetDebouncer(uint32_t holdMs, uint32_t pollIntervalMs)
    : holdMs_(holdMs), pollIntervalMs_(pollIntervalMs) {}

FactoryResetResult FactoryResetDebouncer::feed(bool isPressed) {
    switch (state_) {
        case State::IDLE:
            if (isPressed) {
                state_ = State::HOLDING;
                heldMs_ = pollIntervalMs_;
                // Check threshold immediately after first press (e.g. zero or
                // single-tick threshold must confirm on the first feed).
                if (heldMs_ >= holdMs_) {
                    state_ = State::CONFIRMED;
                }
            }
            break;

        case State::HOLDING:
            if (!isPressed) {
                state_ = State::CANCELLED;
            } else {
                heldMs_ += pollIntervalMs_;
                if (heldMs_ >= holdMs_) {
                    state_ = State::CONFIRMED;
                }
            }
            break;

        case State::CONFIRMED:
            return FactoryResetResult::CONFIRMED;

        case State::CANCELLED:
            return FactoryResetResult::CANCELLED;
    }

    switch (state_) {
        case State::CONFIRMED: return FactoryResetResult::CONFIRMED;
        case State::CANCELLED:  return FactoryResetResult::CANCELLED;
        default:                return FactoryResetResult::WAITING;
    }
}

} // namespace esp32_firmware
