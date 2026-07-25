// LoopHeartbeat.cpp - Periodic loop-state snapshot for serial diagnostics
// Extracted from can-bridge.ino for host testability

#include "LoopHeartbeat.h"

#include "WiFiManager.h"  // WiFiState::State + stateName()

#include <cstdio>

namespace esp32_firmware {

LoopHeartbeat::LoopHeartbeat(uint32_t intervalMs)
    : intervalMs_(intervalMs), lastTickMs_(0) {}

bool LoopHeartbeat::tick(uint32_t nowMs, int wifiState, bool monitorActive) {
    if (nowMs - lastTickMs_ < intervalMs_) {
        return false;
    }
    lastTickMs_ = nowMs;

    const char* stateName = WiFiManager::stateName(static_cast<WiFiState::State>(wifiState));

    char buf[128]{};
    std::snprintf(buf, sizeof(buf),
                  "[STATE] uptime=%lums wifi=%s monitor=%s\r\n",
                  static_cast<unsigned long>(nowMs),
                  stateName,
                  monitorActive ? "ACTIVE" : "idle");
    snapshot_.assign(buf);
    return true;
}

} // namespace esp32_firmware
