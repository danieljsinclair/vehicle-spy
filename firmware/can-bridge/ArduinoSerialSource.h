#pragma once

// ArduinoSerialSource.h - Arduino Serial-backed ISerialSource adapter.
//
// Thin ESP32-boundary implementation of the ISerialSource interface declared in
// firmware/vanilla/SerialCommandFramer.h. Wraps the Arduino Serial object and
// returns raw bytes to the vanilla SerialCommandFramer.
//
// Header-only: no .cpp TU. Only #included by can-bridge.ino (device-only), so no
// #ifdef ARDUINO guard is needed.

#include <Arduino.h>
#include "SerialCommandFramer.h"

namespace esp32_firmware {

// Serial byte source: delegates read() to the global Arduino Serial instance.
// Serial.read() returns -1 when no byte is available — exactly the ISerialSource
// empty sentinel the vanilla framer drains on.
struct ArduinoSerialSource : public ISerialSource {
    int read() override { return Serial.read(); }
};

} // namespace esp32_firmware
