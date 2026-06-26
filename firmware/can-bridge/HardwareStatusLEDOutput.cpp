#include "HardwareStatusLEDOutput.h"

namespace firmware {

// ── Constructor ───────────────────────────────────────────────────────────────────
HardwareStatusLEDOutput::HardwareStatusLEDOutput(int gpioPin)
    : gpioPin_(gpioPin)
    , initialized_(false) {
}

// ── Initialize ───────────────────────────────────────────────────────────────────
void HardwareStatusLEDOutput::init() {
#ifdef ARDUINO
    pinMode(gpioPin_, OUTPUT);
    digitalWrite(gpioPin_, LOW);  // Start with LED OFF
#endif
    initialized_ = true;
}

// ── Set On/Off ───────────────────────────────────────────────────────────────────
void HardwareStatusLEDOutput::setOn(bool on) {
    (void)on;  // Suppress unused parameter warning in non-Arduino builds
#ifdef ARDUINO
    if (initialized_) {
        // ESP32 blue LED is active LOW (LOW = ON, HIGH = OFF)
        digitalWrite(gpioPin_, on ? LOW : HIGH);
    }
#endif
    // Note: On non-Arduino builds (native tests), this is a no-op
    // The test mock implementations are used instead
}

} // namespace firmware
