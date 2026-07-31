// FactoryResetCheck.cpp - Boot-time factory reset check orchestrator
// Extracted from can-bridge.ino for host testability

#include "FactoryResetCheck.h"
#include "FactoryReset.h"

#include <cstdio>

namespace esp32_firmware {

FactoryResetCheck::FactoryResetCheck(uint32_t holdMs, uint32_t pollMs,
                                      IFactoryResetGpio& gpio, IFactoryResetDelay& delay,
                                      IFactoryResetLogger& logger, ICredentialClear& credClear)
    : holdMs_(holdMs), pollMs_(pollMs), gpio_(gpio), delay_(delay),
      logger_(logger), credClear_(credClear) {}

bool FactoryResetCheck::run() {
    // Initial check: if the pin is not pressed, no reset is needed.
    if (!gpio_.isPressed()) {
        return false;
    }

    // Loop-entry diagnostic: matches the original can-bridge.ino Serial.printf
    // that was dropped during extraction. Placed here (after the not-pressed
    // guard, before the debounce loop) to preserve exact behavior parity.
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "Factory reset: GPIO0 held at boot, waiting %lums to confirm...",
                  static_cast<unsigned long>(holdMs_));
    logger_.log(buf, false);

    // Debounce loop: feed GPIO readings to the debouncer until it reaches a
    // terminal state (CONFIRMED or CANCELLED).
    FactoryResetDebouncer debouncer(holdMs_, pollMs_);
    FactoryResetResult result;
    while (true) {
        result = debouncer.feed(gpio_.isPressed());
        if (result != FactoryResetResult::WAITING) {
            break;
        }
        delay_.delayMs(pollMs_);
    }

    // Interpret the result.
    if (result == FactoryResetResult::CONFIRMED) {
        // Confirmed path: RED severity (original can-bridge.ino used RED here).
        logger_.log("Factory reset: clearing WiFi credentials and booting to AP mode", true);
        credClear_.clear();
        return true;
    }

    // Cancelled path: YELLOW severity.
    logger_.log("Factory reset: released early, cancelling", false);
    return false;
}

} // namespace esp32_firmware
