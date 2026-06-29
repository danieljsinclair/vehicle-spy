#ifndef FIRMWARE_HARDWARE_STATUS_LED_OUTPUT_H
#define FIRMWARE_HARDWARE_STATUS_LED_OUTPUT_H

#include "IStatusLEDOutput.h"

// Arduino/ESP32 specific includes for hardware LED control
#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace firmware {

// ── Hardware Status LED Output (ESP32 implementation) ─────────────────────────────
// Real implementation of IStatusLEDOutput for ESP32 firmware.
// Controls the blue LED on GPIO2 using Arduino digitalWrite/pinMode.
class HardwareStatusLEDOutput : public IStatusLEDOutput {
public:
    // Constructor with configurable GPIO pin (default GPIO2 for ESP32 blue LED)
    explicit HardwareStatusLEDOutput(int gpioPin = 2);

    // Initialize the LED hardware (configure pin mode)
    void init() override;

    // Set the LED hardware state (true = ON, false = OFF)
    void setOn(bool on) override;

private:
    int gpioPin_;
    bool initialized_;

    // Use the members in non-Arduino builds to suppress warnings
    #ifndef ARDUINO
    void suppressUnusedWarnings() {
        (void)gpioPin_;
        (void)initialized_;
    }
    #endif
};

} // namespace firmware

#endif // FIRMWARE_HARDWARE_STATUS_LED_OUTPUT_H
