#ifndef FIRMWARE_ISTATUS_LED_OUTPUT_H
#define FIRMWARE_ISTATUS_LED_OUTPUT_H

#include <cstdint>

namespace firmware {

// ── Status LED Output Interface (hardware abstraction for testability) ───────
// This interface allows the StatusLED state machine to be unit-tested with mocks.
// The real implementation (HardwareStatusLEDOutput) calls digitalWrite/pinMode.
// Mock implementations record state transitions for test assertions.
class IStatusLEDOutput {
public:
    virtual ~IStatusLEDOutput() = default;

    // Set the LED hardware state (true = ON, false = OFF)
    virtual void setOn(bool on) = 0;

    // Initialize the LED hardware (configure pin mode, etc.)
    virtual void init() = 0;
};

} // namespace firmware

#endif // FIRMWARE_ISTATUS_LED_OUTPUT_H
