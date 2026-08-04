#pragma once

// ArduinoDebugSerial.h - Arduino Serial-backed ISerial adapter.
//
// Thin ESP32-boundary implementation of the ISerial debug-trace interface
// declared in firmware/vanilla/WiFiManager.h. Forwards WiFi state-transition
// traces to the global Arduino Serial object.
//
// Header-only: no .cpp TU. Only #included by can-bridge.ino (device-only), so no
// #ifdef ARDUINO guard is needed.

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>

#include "WiFiManager.h"

namespace esp32_firmware {

// Formats into a stack buffer and emits via Serial.print. Serial.printf is not
// portable across all cores, and pre-formatting keeps the emitted line atomic
// (a single write) rather than interleaved with other Serial traffic.
struct ArduinoDebugSerial : public ISerial {
    void println(const char* msg) override { Serial.println(msg); }

    __attribute__((format(printf, 2, 3)))
    void printf(const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        char buffer[192];
        const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (written > 0) {
            Serial.print(buffer);
        }
    }
};

} // namespace esp32_firmware
