// CanDriver.cpp - TWAI driver initialization
// Extracted from can-bridge.ino setup() for host testability

#include "CanDriver.h"

namespace esp32_firmware {

CanDriver::CanDriver(ILogger& logger, ITwaiHardware& hardware, bool enabled)
    : logger_(logger), hardware_(hardware), enabled_(enabled) {}

bool CanDriver::initialize(CanGeneralConfig* gcfg, CanTimingConfig* tcfg, CanFilterConfig* fcfg) {
    if (!enabled_) {
        logger_.log("TWAI disabled via VEHICLE_SIM_ENABLE_TWAI=0", false);
        return true;  // Not an error — feature compiled out.
    }

    if (hardware_.driverInstall(gcfg, tcfg, fcfg) != 0) {
        logger_.log("FAIL: twai_driver_install", true);
        return false;
    }
    if (hardware_.start() != 0) {
        logger_.log("FAIL: twai_start", true);
        return false;
    }
    logger_.log("TWAI started @ 500kbps (listen-only)", false);
    return true;
}

} // namespace esp32_firmware
