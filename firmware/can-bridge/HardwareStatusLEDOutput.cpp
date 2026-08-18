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
        // ESP32-WROOM-32 devkit onboard blue LED (GPIO2) is ACTIVE-HIGH:
        // HIGH = ON, LOW = OFF. This was previously driven active-LOW, which
        // inverted every pattern on hardware — solid-ON requests rendered the
        // LED dark (a client connect extinguished it) and error sequences read
        // as inverted blips. Serial traces looked correct throughout because
        // the setPattern/step commands were green; only eyes on the board
        // could catch the inversion.
        digitalWrite(gpioPin_, on ? HIGH : LOW);
    }
#endif
    // Note: On non-Arduino builds (native tests), this is a no-op
    // The test mock implementations are used instead
}

} // namespace firmware
