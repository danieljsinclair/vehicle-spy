#pragma once

// ArduinoTimeNtp.h - Arduino time implementation for ITimeNtp interface
// Bridges Arduino time functions to vanilla ITimeNtp interface
//
// This is the production implementation used in the .ino.
// For host testing, use MockTimeNtp from tests.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO defined).
// Host tests use mocks instead.

#ifdef ARDUINO

#include <ctime>
#include <cstring>
#include "NtpTimeSync.h"

namespace esp32_firmware {

// ArduinoTimeNtp - production ITimeNtp implementation using Arduino time functions
class ArduinoTimeNtp : public ITimeNtp {
public:
    ArduinoTimeNtp() = default;

    // ITimeNtp interface - delegates to Arduino time functions
    void setenv(const char* name, const char* value, int overwrite) override {
        ::setenv(name, value, overwrite);
    }

    void tzset() override {
        ::tzset();
    }

    time_t time(time_t* t) override {
        return ::time(t);
    }

    void gmtime_r(const time_t* timep, struct tm* result) override {
        ::gmtime_r(timep, result);
    }

    size_t strftime(char* s, size_t maxsize, const char* format, const struct tm* tm) override {
        return ::strftime(s, maxsize, format, tm);
    }
};

} // namespace esp32_firmware

#endif // ARDUINO
