// LedPatternPolicy.h - LED pattern selection + application policy
// Extracted from FirmwareApp::updateLedPattern + related accessors for S1448
//
// Owns the per-tick LED write: reads WiFi state + client-connected flag,
// selects the appropriate pattern, and applies it to the status LED.

#pragma once

#include "WiFiManager.h"
#include "ISerialEventLogger.h"

namespace esp32_firmware {

struct IStatusLED;
class WiFiManager;

// LedPatternPolicy: LED pattern selection and application policy.
// Separated from FirmwareApp so the main orchestrator stays under the
// S1448 method-count threshold.
class LedPatternPolicy {
public:
    LedPatternPolicy(IStatusLED& statusLed, WiFiManager& wifiManager,
                     std::function<void()> restartTcpServer,
                     std::function<void()> broadcastDiscovery);

    // Single per-tick LED write: selects pattern from (wifiState, clientConnected)
    // and applies it. Called from FirmwareApp::update().
    void update(uint32_t now, bool clientConnected);

    // Query: does the TCP server need restarting?
    bool shouldRestartTcpServer() const;

    // Clear the TCP server restart flag.
    void clearTcpServerRestartFlag();

    // Last LED pattern value (for heartbeat / observability).
    int currentPattern() const { return lastLedPattern_; }

private:
    IStatusLED& statusLed_;
    WiFiManager& wifiManager_;
    std::function<void()> restartTcpServer_;
    std::function<void()> broadcastDiscovery_;
    int lastLedPattern_ = 0;
};

} // namespace esp32_firmware
