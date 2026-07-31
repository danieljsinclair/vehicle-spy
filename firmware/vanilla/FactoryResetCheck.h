#pragma once

// FactoryResetCheck.h - Boot-time factory reset check orchestrator
// Extracted from can-bridge.ino for host testability
//
// Owns the full boot-time factory reset flow: initial GPIO check, debounce loop,
// and result interpretation. The .ino supplies thin Arduino adapters for the
// hardware boundaries (GPIO read, delay, serial log, credential clear).
//
// This replaces the inline checkFactoryReset() function (31 lines, untested).

#include <cstdint>

namespace esp32_firmware {

// GPIO read boundary: returns true when the factory-reset pin is active (LOW).
struct IFactoryResetGpio {
    virtual bool isPressed() = 0;
    virtual ~IFactoryResetGpio() = default;
};

// Delay boundary: milliseconds pause (drives the poll cadence).
struct IFactoryResetDelay {
    virtual void delayMs(uint32_t ms) = 0;
    virtual ~IFactoryResetDelay() = default;
};

// Log boundary: diagnostic output (replaces Serial.printf in the .ino).
// isConfirmed selects severity: true → RED (reset confirmed), false → YELLOW.
struct IFactoryResetLogger {
    virtual void log(const char* msg, bool isConfirmed) = 0;
    virtual ~IFactoryResetLogger() = default;
};

// Credential-clear boundary: wipes stored WiFi credentials on confirmed reset.
struct ICredentialClear {
    virtual void clear() = 0;
    virtual ~ICredentialClear() = default;
};

// FactoryResetCheck drives the full boot-time factory-reset flow.
// Construction holds the config + injected boundaries; run() executes once.
class FactoryResetCheck {
public:
    FactoryResetCheck(uint32_t holdMs, uint32_t pollMs,
                      IFactoryResetGpio& gpio, IFactoryResetDelay& delay,
                      IFactoryResetLogger& logger, ICredentialClear& credClear);

    // Execute the check. Returns true if the reset was confirmed and credentials
    // were cleared; false if the pin was not pressed or the hold was cancelled.
    bool run();

private:
    uint32_t holdMs_;
    uint32_t pollMs_;
    IFactoryResetGpio& gpio_;
    IFactoryResetDelay& delay_;
    IFactoryResetLogger& logger_;
    ICredentialClear& credClear_;
};

} // namespace esp32_firmware
