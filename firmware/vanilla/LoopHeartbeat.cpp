// LoopHeartbeat.cpp - Periodic loop-state snapshot for serial diagnostics
// Extracted from can-bridge.ino for host testability

#include "LoopHeartbeat.h"

#include "FirmwareVersion.h"  // FIRMWARE_BUILD_VERSION (single source of truth)
#include "WiFiManager.h"  // WiFiState::State + stateName()

#include <cstdio>

namespace esp32_firmware {

LoopHeartbeat::LoopHeartbeat(uint32_t intervalMs)
    : intervalMs_(intervalMs), lastTickMs_(0) {}

bool LoopHeartbeat::tick(uint32_t nowMs, int wifiState, bool monitorActive,
                          const std::string& clientIp, const std::string& discoveryCadence,
                          int ledPattern, const std::string& targetSsid,
                          const std::string& ownIp, const std::string& authDetail) {
    if (nowMs - lastTickMs_ < intervalMs_) {
        return false;
    }
    lastTickMs_ = nowMs;

    const char* stateName = WiFiManager::stateName(static_cast<WiFiState::State>(wifiState));
    const char* clientField = clientIp.empty() ? "none" : clientIp.c_str();
    const char* ssidField = targetSsid.empty() ? "none" : targetSsid.c_str();
    const char* ipField = ownIp.empty() ? "none" : ownIp.c_str();

    char buf[384]{};
    // fw= is APPENDED at the end of the line (both branches): existing
    // consumers match prefixes / earlier fields, so new fields must only ever
    // land after the last one. The value is the build-identifying version
    // (semver + git hash + date) so a [STATE] line names the exact build.
    if (authDetail.empty()) {
        std::snprintf(buf, sizeof(buf),
                      "[STATE] uptime=%lums wifi=%s ssid=%s ip=%s client=%s disc=%s led=%d monitor=%s fw=%s\r\n",
                      static_cast<unsigned long>(nowMs),
                      stateName,
                      ssidField,
                      ipField,
                      clientField,
                      discoveryCadence.c_str(),
                      ledPattern,
                      monitorActive ? "ACTIVE" : "idle",
                      FIRMWARE_BUILD_VERSION);
    } else {
        std::snprintf(buf, sizeof(buf),
                      "[STATE] uptime=%lums wifi=%s ssid=%s ip=%s client=%s disc=%s led=%d monitor=%s auth=%s fw=%s\r\n",
                      static_cast<unsigned long>(nowMs),
                      stateName,
                      ssidField,
                      ipField,
                      clientField,
                      discoveryCadence.c_str(),
                      ledPattern,
                      monitorActive ? "ACTIVE" : "idle",
                      authDetail.c_str(),
                      FIRMWARE_BUILD_VERSION);
    }
    snapshot_.assign(buf);
    return true;
}

} // namespace esp32_firmware
