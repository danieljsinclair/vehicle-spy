// LedPatternPolicy.cpp - LED pattern policy implementation

#include "LedPatternPolicy.h"
#include "StatusLED.h"

namespace esp32_firmware {

LedPatternPolicy::LedPatternPolicy(IStatusLED& statusLed, WiFiManager& wifiManager,
                                   std::function<void()> restartTcpServer,
                                   std::function<void()> broadcastDiscovery)
    : statusLed_(statusLed)
    , wifiManager_(wifiManager)
    , restartTcpServer_(std::move(restartTcpServer))
    , broadcastDiscovery_(std::move(broadcastDiscovery)) {
}

void LedPatternPolicy::update(uint32_t now, bool clientConnected) {
    lastLedPattern_ = static_cast<int>(firmware::StatusLED::selectLedPattern(
        static_cast<int>(wifiManager_.getState()), clientConnected));
    statusLed_.setPattern(lastLedPattern_);
    statusLed_.update(now);

    // Restart TCP server if WiFi reconnected with new IP.
    // NOTE: we only CALL the restart callback here; we do NOT clear the flag.
    // The caller (FirmwareApp::update or the .ino's restartTcpServerIfNeeded)
    // clears the flag AFTER the actual restart succeeds, so that
    // shouldRestartTcpServer() remains true across the tick boundary for any
    // observer that polls it after update() returns.
    if (wifiManager_.shouldRestartTcpServer() && restartTcpServer_) {
        restartTcpServer_();
    }
}

bool LedPatternPolicy::shouldRestartTcpServer() const {
    return wifiManager_.shouldRestartTcpServer();
}

void LedPatternPolicy::clearTcpServerRestartFlag() {
    wifiManager_.clearTcpServerRestartFlag();
}

} // namespace esp32_firmware
