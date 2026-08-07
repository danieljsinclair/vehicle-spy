#pragma once

#include "vehicle-sim/cli/IKeyboardInput.h"
#include "vehicle-sim/util/IClock.h"

#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace vehicle_sim::cli {

/**
 * Interactive run context — keyboard-driven telemetry emission for bench
 * testing of --live-telemetry latency (no car).
 *
 * Reads keyboard input (1-9 = 10-90% throttle, 0 = 100%, q = quit,
 * arrows = gear/steering, b = brake) and emits CSV on stdout at a fixed
 * tick rate (~50 Hz). The keyboard mapping is shared with CSV replay via
 * KeyboardThrottle (DRY). The real terminal keyboard is created via a factory
 * that can be injected for tests (so a test never touches stdin).
 */
class InteractiveRunContext {
public:
    /**
     * Run interactive mode.
     *
     * @param vehicleId    Value for the vehicle_id column.
     * @param intervalMs   Tick interval in milliseconds (default 20 ms = 50 Hz).
     * @param out          Output stream for the CSV rows.
     * @param clock        Clock used for tick pacing (FakeClock in tests).
     * @param makeKeyboard Factory returning the owned keyboard input. Defaults
     *                     to the real terminal-backed KeyboardInput; tests
     *                     inject a fake so no terminal is touched.
     * @return 0 on clean exit (user pressed 'q'), 1 on error.
     */
    static int run(
        const std::string& vehicleId,
        int intervalMs,
        std::ostream& out,
        vehicle_sim::util::IClock& clock,
        std::function<std::unique_ptr<IKeyboardInput>()> makeKeyboard = defaultKeyboard()
    );

private:
    static std::function<std::unique_ptr<IKeyboardInput>()> defaultKeyboard();
};

} // namespace vehicle_sim::cli
