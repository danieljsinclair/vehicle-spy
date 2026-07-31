#pragma once

// NvsWifiCredentialStore.h - Vanilla WiFi credential NVS write logic
// Extracted from can-bridge.ino for host testability
//
// Owns the NVS write contract for WiFi credentials: select the "wifi" namespace,
// write both SSID and password, and report whether both writes succeeded. The .ino
// supplies an INvsWifiStore implementation backed by Arduino Preferences; vanilla
// code (and host tests) use a fake/mock.
//
// This replaces the inline storeWifiCredentials() function (7 lines, untested)
// that previously called the concrete Arduino Preferences class directly.

#include <string>

namespace esp32_firmware {

// NVS write boundary: abstracts the three Preferences operations needed to
// persist a key/value pair inside a named namespace. Implementations back this
// with Arduino Preferences (production) or an in-memory fake (tests).
struct INvsWifiStore {
    virtual void begin(const char* name, bool readOnly) = 0;
    virtual size_t putString(const char* key, const std::string& value) = 0;
    virtual void end() = 0;
    virtual ~INvsWifiStore() = default;
};

// Write WiFi credentials to NVS.
//
// Selects the "wifi" namespace, writes the SSID under the "ssid" key and the
// password under the "pass" key, then closes the namespace. Returns true only
// if both putString calls report a non-zero byte count (both writes must
// succeed; a partial write leaves the namespace in an inconsistent state).
//
// This is a pure function over the INvsWifiStore boundary: no side effects
// beyond the store operations, no Arduino dependency, fully host-testable.
bool storeWifiCredentials(INvsWifiStore& store,
                          const std::string& ssid,
                          const std::string& pass);

} // namespace esp32_firmware
