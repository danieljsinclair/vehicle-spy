#pragma once

// ArduinoWiFi.h - Arduino WiFi implementation for IWiFi interface
// Bridges Arduino WiFi library to vanilla IWiFi interface
//
// This is the production implementation used in the .ino.
// For host testing, use WiFiMock from mocks/WiFiMock.h.
//
// IMPORTANT: This file is only available when building for Arduino (ARDUINO defined).
// Host tests use mocks instead.

#ifdef ARDUINO

#include <WiFi.h>
#include <string>
#include <functional>
#include "WiFiManager.h"
#include "DiscoveryManager.h"

namespace esp32_firmware {

// Forward declaration
struct IWiFiDiscovery;

// ArduinoWiFi - production IWiFi implementation using Arduino WiFi library
// Also implements IWiFiDiscovery for DiscoveryManager
class ArduinoWiFi : public IWiFi, public IWiFiDiscovery {
public:
    ArduinoWiFi() = default;

    // IWiFi interface - delegates to WiFi class
    void setMode(int mode) override {
        WiFi.mode(static_cast<wifi_mode_t>(mode));
    }

    void begin(const char* ssid, const char* pass) override {
        if (pass && *pass) {
            WiFi.begin(ssid, pass);
        } else {
            WiFi.begin(ssid);
        }
    }

    void disconnect(bool wifiOff, bool eraseAP) override {
        WiFi.disconnect(wifiOff, eraseAP);
    }

    int status() const override {
        return static_cast<int>(WiFi.status());
    }

    std::string localIP() const override {
        return WiFi.localIP().toString().c_str();
    }

    std::string softAPIP() const override {
        return WiFi.softAPIP().toString().c_str();
    }

    void softAP(const char* ssid, const char* pass) override {
        if (pass && *pass) {
            WiFi.softAP(ssid, pass);
        } else {
            WiFi.softAP(ssid);
        }
    }

    void setHostname(const char* name) override {
        WiFi.setHostname(name);
    }

    int getMode() const override {
        return static_cast<int>(WiFi.getMode());
    }

    std::string SSID() const override {
        return WiFi.SSID().c_str();
    }

    const char* disconnectReasonName(int reason) const override {
        return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
    }

    void onEvent(std::function<void(int, void*)> cb, int event) override {
        // Store callback and register with WiFi
        // Note: Arduino WiFiEvent requires a different signature
        // We'll need to adapt this - placeholder for TDD skeleton
        (void)cb;
        (void)event;
        // TODO: Implement proper event callback registration
        // WiFi.onEvent([cb](WiFiEvent_t event, WiFiEventInfo_t info) {
        //     cb(static_cast<int>(event), &info);
        // }, static_cast<WiFiEvent_t>(event));
    }

    // IWiFiDiscovery interface - for DiscoveryManager
    std::string broadcastIP() const override {
        if (WiFi.getMode() == WIFI_AP) {
            return "192.168.4.255";
        } else if (WiFi.getMode() == WIFI_STA) {
            return WiFi.localIP().toString().c_str();
            // Note: This doesn't calculate the actual broadcast IP
            // For STA mode, should use subnet mask to calculate broadcast address
            // For now, return local IP as placeholder
        }
        return "255.255.255.255";
    }
};

} // namespace esp32_firmware

#endif // ARDUINO
