#pragma once

// LoopHeartbeat.h - Periodic loop-state snapshot for serial diagnostics
// Extracted from can-bridge.ino for host testability
//
// Produces a formatted "[STATE] uptime=<ms> wifi=<name> monitor=<ACTIVE|idle>\r\n"
// line at a configurable interval. The .ino calls tick() each loop iteration
// with millis() and the live firmware state; when the interval has elapsed,
// tick() returns true and snapshot() holds the formatted line.
//
// C++14 compatible: std::optional is NOT used (ESP32 toolchain is gnu++14).
// tick() returns bool; the formatted line is accessed via snapshot().
//
// Pure logic — no Arduino/ESP32 dependencies. The state-name mapping delegates
// to WiFiManager::stateName() (already tested in WiFiManager_test.cpp).

#include <cstdint>
#include <string>

namespace esp32_firmware {

class LoopHeartbeat {
public:
    explicit LoopHeartbeat(uint32_t intervalMs);

    // Advance the heartbeat. Returns true if the interval has elapsed since
    // the last tick (snapshot() is now valid), false otherwise.
    // clientIp: remote IP of the connected TCP client, or empty for none.
    // discoveryCadence: current discovery broadcast cadence (e.g. "500ms", "10s").
    // ledPattern: current LED pattern enum value.
    // Defaults preserve the pre-observability signature for existing callers.
    bool tick(uint32_t nowMs, int wifiState, bool monitorActive,
              const std::string& clientIp = "", const std::string& discoveryCadence = "none",
              int ledPattern = 0, const std::string& targetSsid = "",
              const std::string& authDetail = "");

    // Access the last formatted snapshot line. Valid only after tick() returned
    // true on the most recent call.
    const std::string& snapshot() const { return snapshot_; }

private:
    uint32_t intervalMs_;
    uint32_t lastTickMs_;
    std::string snapshot_;
};

} // namespace esp32_firmware
