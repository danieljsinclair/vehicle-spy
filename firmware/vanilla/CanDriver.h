#pragma once

// CanDriver.h - TWAI driver initialization
// Extracted from can-bridge.ino setup() for host testability
//
// Owns the TWAI driver install/start sequence: enabled/disabled branch,
// driverInstall result check, start result check, and diagnostic logging.
// The .ino supplies thin Arduino adapters for the hardware boundaries
// (actual TWAI calls + Serial output with color codes).
//
// This replaces the inline TWAI init block (17 lines, untested).

namespace esp32_firmware {

// Forward-declared ESP-IDF TWAI config handles (opaque in vanilla).
// Defined in <driver/twai.h> in the .ino; vanilla never dereferences them.
struct CanGeneralConfig;
struct CanTimingConfig;
struct CanFilterConfig;

// Logger boundary: diagnostic output with severity.
// isError selects severity: true → RED (fatal error), false → plain (info).
struct ILogger {
    virtual void log(const char* msg, bool isError) = 0;
    virtual ~ILogger() = default;
};

// TWAI hardware boundary: wraps ESP-IDF driver calls.
struct ITwaiHardware {
    virtual int driverInstall(CanGeneralConfig* gcfg, CanTimingConfig* tcfg, CanFilterConfig* fcfg) = 0;
    virtual int start() = 0;
    virtual ~ITwaiHardware() = default;
};

// CanDriver handles TWAI driver initialization sequence.
// Construction holds config + injected boundaries; initialize() executes once.
class CanDriver {
public:
    CanDriver(ILogger& logger, ITwaiHardware& hardware, bool enabled);

    // Execute the TWAI init sequence. Returns true on success (or if disabled).
    // On failure, logs the error and returns false — the caller decides the
    // recovery strategy (in the .ino: hang forever to preserve original fatal
    // behavior; in tests: assert and continue).
    bool initialize(CanGeneralConfig* gcfg, CanTimingConfig* tcfg, CanFilterConfig* fcfg);

    // Check if TWAI is enabled at build time.
    bool isEnabled() const { return enabled_; }

private:
    ILogger& logger_;
    ITwaiHardware& hardware_;
    bool enabled_;
};

} // namespace esp32_firmware
