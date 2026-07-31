#pragma once

// ISerialEventLogger.h - Centralized event/state logging interface
//
// FirmwareApp owns ONE IEventLogger and emits ALL [EVENT] and [STATE] lines
// through it. The .ino supplies a concrete implementation (e.g. SerialEventLogger
// that writes to Serial); tests inject a mock to verify the contract.
//
// This is the observability seam: all serial diagnostics flow through this single
// interface, eliminating the prior scattered Serial.printf calls and the 24-file
// test-fixture ripple that logger-injection-per-manager caused.

#include <string>

namespace esp32_firmware {

struct IEventLogger {
    // Log a one-shot [EVENT] line. detail contains the pre-formatted key=value
    // payload (e.g. "ip=192.168.1.42" or "reason=4").
    virtual void logEvent(const char* eventType, const std::string& detail) = 0;

    // Log a periodic [STATE] line. line is the complete formatted snapshot
    // (e.g. "[STATE] uptime=5000ms wifi=WIFI_CONNECTED client=... monitor=...\r\n").
    virtual void logState(const std::string& line) = 0;

    virtual ~IEventLogger() = default;
};

} // namespace esp32_firmware
