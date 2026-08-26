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
        // ESP32 Preferences::getBytesLength returns 0 for STRING-typed keys
        // (it is sized for blobs), yet this interface is used to probe key
        // PRESENCE before loading credentials. A string key therefore reads as
        // "absent" and the device wrongly falls back to AP mode despite valid
        // stored creds. Fall back to the string length so presence-probing is
        // correct for the string values this namespace actually stores.
        size_t blobLen = prefs_.getBytesLength(key);
        if (blobLen > 0) return blobLen;
        String s = prefs_.getString(key, String());
        return s.length();
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

    // Expose the underlying Arduino Preferences handle so callers that need the
    // concrete type (e.g. ArduinoAtWifiStore, which is built directly over a
    // Preferences&) can share THIS exact NVS handle. WiFi credentials must be
    // written and read through one backing store, or the AT store and the
    // boot-time reader silently diverge (see WifiCredentialSharedBacking_test).
    Preferences& rawPreferences() { return prefs_; }

private:
    Preferences prefs_;
};

} // namespace esp32_firmware

#endif // ARDUINO
