#pragma once

// ArduinoPreferences.h - Arduino Preferences implementation for IPreferences interface
// Bridges Arduino Preferences library to vanilla IPreferences interface
//
// This is the production implementation used in the .ino.
// For host testing, use PreferencesMock from mocks/PreferencesMock.h.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO defined).
// Host tests use mocks instead.

#ifdef ARDUINO

#include <Preferences.h>
#include <string>
#include "WiFiManager.h"

namespace esp32_firmware {

// ArduinoPreferences - production IPreferences implementation using Arduino Preferences library
class ArduinoPreferences : public IPreferences {
public:
    ArduinoPreferences() = default;

    // IPreferences interface - delegates to Preferences class
    void begin(const char* name, bool readOnly) override {
        prefs_.begin(name, readOnly);
    }

    void end() override {
        prefs_.end();
    }

    size_t getBytesLength(const char* key) override {
        return prefs_.getBytesLength(key);
    }

    std::string getString(const char* key, const std::string& defaultValue) override {
        // Use the String-returning Preferences overload (no fixed-size C buffer).
        // Preserve the original semantics: an absent OR empty-stored value yields
        // the caller's default.
        String result = prefs_.getString(key, String());
        if (result.isEmpty()) {
            return defaultValue;
        }
        return std::string(result.c_str());
    }

    size_t putString(const char* key, const std::string& value) override {
        return prefs_.putString(key, value.c_str());
    }

    void clear() override {
        prefs_.clear();
    }

private:
    Preferences prefs_;
};

} // namespace esp32_firmware

#endif // ARDUINO
