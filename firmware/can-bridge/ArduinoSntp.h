#pragma once

// ArduinoSntp.h - Arduino SNTP implementation for ISntp interface
// Bridges Arduino sntp functions to vanilla ISntp interface
//
// This is the production implementation used in the .ino.
// For host testing, use MockSntp from tests.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO defined).
// Host tests use mocks instead.

#ifdef ARDUINO

#include <sntp.h>
#include <functional>
#include "NtpTimeSync.h"

namespace esp32_firmware {

// Callback wrapper to convert std::function to C function pointer
static std::function<void(struct timeval*)> g_sntpCallbackWrapper;

extern "C" void sntp_callback_wrapper(struct timeval* tv) {
    if (g_sntpCallbackWrapper) {
        g_sntpCallbackWrapper(tv);
    }
}

// ArduinoSntp - production ISntp implementation using Arduino sntp library
class ArduinoSntp : public ISntp {
public:
    ArduinoSntp() = default;

    // ISntp interface - delegates to Arduino sntp functions
    bool enabled() const override {
        return sntp_enabled();
    }

    void setOperatingMode(int mode) override {
        sntp_setoperatingmode(mode);
    }

    void setServerName(int idx, const char* server) override {
        sntp_setservername(idx, server);
    }

    void setSyncMode(int mode) override {
        sntp_set_sync_mode(static_cast<sntp_sync_mode_t>(mode));
    }

    void setSyncInterval(int32_t intervalMs) override {
        sntp_set_sync_interval(intervalMs);
    }

    void setTimeSyncNotificationCallback(std::function<void(struct timeval*)> cb) override {
        g_sntpCallbackWrapper = std::move(cb);
        sntp_set_time_sync_notification_cb(sntp_callback_wrapper);
    }

    void init() override {
        sntp_init();
    }
};

} // namespace esp32_firmware

#endif // ARDUINO
