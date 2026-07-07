#pragma once

// ArduinoTime.h - Arduino Time implementation for ITime interface
// Bridges Arduino time functions to vanilla ITime interface
//
// This is the production implementation used in the .ino.
// For host testing, use MockTime from tests.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO defined).
// Host tests use mocks instead.

#ifdef ARDUINO

#include <cstdint>
#include <sys/time.h>
#include "DiscoveryManager.h"

namespace esp32_firmware {

// ArduinoTime - production ITime implementation using Arduino time functions
class ArduinoTime : public ITime {
public:
    ArduinoTime() = default;

    // ITime interface - delegates to Arduino time functions
    uint64_t getCurrentTimestamp() const override {
        // Get current Unix timestamp
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return static_cast<uint64_t>(tv.tv_sec);
    }

    uint32_t millis() const override {
        return ::millis();
    }
};

} // namespace esp32_firmware

#endif // ARDUINO
