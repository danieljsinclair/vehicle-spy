#pragma once

// ArduinoAtAdapters.h - AT-command Arduino adapter implementations.
//
// Header-only thin Arduino implementations of the vanilla ITcpClientAt/ISerialAt/
// IEspAt/IWifiCredentialStore interfaces declared in firmware/vanilla/
// AtCommandDispatcher.h. Each adapter is constructed by the .ino with the
// device-specific values it already owns (WiFiClient reference, reboot delay,
// Preferences reference); the vanilla AtCommandDispatcher is unaware of
// Arduino types.
//
// Header-only: no .cpp TU. Only #included by can-bridge.ino (device-only), so no
// #ifdef ARDUINO guard is needed.

#include <Arduino.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include "AtCommandDispatcher.h"
#include "WiFiManager.h"  // loadCredentialsImpl (boot-time credential reader)

namespace esp32_firmware {

// TCP client adapter: wraps the .ino's connected-buddy WiFiClient.
// The WiFiClient reference is injected at construction so the adapter is
// independent of the global client() accessor.
struct ArduinoAtTcpClient : public ITcpClientAt {
    explicit ArduinoAtTcpClient(WiFiClient& client) : client_(client) {}
    void print(const char* str) override { client_.print(str); }
    void flush() override { client_.flush(); }
private:
    WiFiClient& client_;
};

// Serial adapter: wraps Serial (USB diagnostic logging).
struct ArduinoAtSerial : public ISerialAt {
    void println(const char* str) override { Serial.println(str); }
    void flush() override { Serial.flush(); }
};

// ESP adapter: restarts the chip after the configured delay.
struct ArduinoAtEsp : public IEspAt {
    explicit ArduinoAtEsp(uint32_t rebootDelayMs) : rebootDelayMs_(rebootDelayMs) {}
    void restart() override {
        delay(rebootDelayMs_);
        ESP.restart();
    }
private:
    uint32_t rebootDelayMs_;
};

// NVS store inner adapter: bridges the concrete Arduino Preferences class over
// the vanilla INvsWifiStore interface. Defined as a standalone struct (not a
// local class inside ArduinoAtWifiStore::store) so it can be constructed with
// the injected Preferences reference and passed to storeWifiCredentials.
struct ArduinoNvsWifiStore : public INvsWifiStore {
    explicit ArduinoNvsWifiStore(Preferences& prefs) : prefs_(prefs) {}
    void begin(const char* name, bool readOnly) override { prefs_.begin(name, readOnly); }
    size_t putString(const char* key, const std::string& value) override {
        return prefs_.putString(key, value.c_str());
    }
    void end() override { prefs_.end(); }
private:
    Preferences& prefs_;
};

// WiFi credential store adapter: bridges the production ArduinoPreferences
// (the IPreferences impl FirmwareApp/WiFiManager use at boot) to the vanilla
// IWifiCredentialStore interface. Holding the SAME ArduinoPreferences instance
// the boot reader uses guarantees ATSETWIFI writes land in the handle boot
// reads — a single source of truth for WiFi creds.
struct ArduinoAtWifiStore : public IWifiCredentialStore {
    explicit ArduinoAtWifiStore(ArduinoPreferences& prefs) : prefs_(prefs) {}

    bool store(const std::string& ssid, const std::string& password) override {
        ArduinoNvsWifiStore adapter(prefs_.rawPreferences());
        return esp32_firmware::storeWifiCredentials(adapter, ssid, password);
    }

    bool load(std::string& ssid, std::string& pass) override {
        return esp32_firmware::loadCredentialsImpl(prefs_, ssid, pass);
    }
private:
    ArduinoPreferences& prefs_;
};

} // namespace esp32_firmware
