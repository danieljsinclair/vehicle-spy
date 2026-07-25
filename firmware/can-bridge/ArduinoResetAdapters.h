#pragma once

// ArduinoResetAdapters.h - Arduino hardware adapters for the factory-reset check.
//
// Thin ESP32-boundary implementations of the IFactoryResetGpio / IFactoryResetDelay
// / IFactoryResetLogger interfaces declared in firmware/vanilla/FactoryResetCheck.h.
// Each adapter is constructed by the .ino with the device-specific values it already
// owns (GPIO pin number, ANSI colour strings); the vanilla FactoryResetCheck is
// unaware of Arduino types.
//
// Header-only: no .cpp TU. Only #included by can-bridge.ino (device-only), so no
// #ifdef ARDUINO guard is needed.

#include <Arduino.h>
#include "FactoryResetCheck.h"

namespace esp32_firmware {

// GPIO read boundary: returns true when the factory-reset pin reads LOW.
struct ArduinoResetGpio : public IFactoryResetGpio {
    explicit ArduinoResetGpio(gpio_num_t pin) : pin_(pin) {}
    bool isPressed() override { return digitalRead(pin_) == LOW; }
private:
    gpio_num_t pin_;
};

// Delay boundary: milliseconds pause (drives the debounce poll cadence).
struct ArduinoResetDelay : public IFactoryResetDelay {
    void delayMs(uint32_t ms) override { delay(ms); }
};

// Log boundary: diagnostic output for the factory-reset flow.
// isConfirmed selects severity: true -> RED (reset confirmed), false -> YELLOW.
struct ArduinoResetLogger : public IFactoryResetLogger {
    ArduinoResetLogger(const char* red, const char* yellow, const char* nc)
        : red_(red), yellow_(yellow), nc_(nc) {}
    void log(const char* msg, bool isConfirmed) override {
        Serial.printf("%s%s%s\r\n", isConfirmed ? red_ : yellow_, msg, nc_);
    }
private:
    const char* red_;
    const char* yellow_;
    const char* nc_;
};

} // namespace esp32_firmware
