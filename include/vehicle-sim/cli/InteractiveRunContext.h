#pragma once

#include "vehicle-sim/interactive/IKeyboardInput.h"
#include "vehicle-sim/util/IClock.h"

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

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
 *
 * The keyboard factory is a template parameter (defaulting to std::function)
 * rather than a std::function by-value argument, so callers inject a test fake
 * without paying for type erasure and without a CRITICAL Sonar finding.
 */
class InteractiveRunContext {
public:
    /// Default factory: produces the real terminal-backed KeyboardInput.
    using KeyboardFactory = std::function<std::unique_ptr<interactive::IKeyboardInput>()>;

    /**
     * Run interactive mode.
     *
     * @tparam KeyboardFactoryT Callable type returning a unique_ptr<IKeyboardInput>.
     * @param vehicleId    Value for the vehicle_id column.
     * @param intervalMs   Tick interval in milliseconds (default 20 ms = 50 Hz).
     * @param out          Output stream for the CSV rows.
     * @param clock        Clock used for tick pacing (FakeClock in tests).
     * @param makeKeyboard Factory returning the owned keyboard input. Defaults
     *                     to the real terminal-backed KeyboardInput; tests
     *                     inject a fake so no terminal is touched.
     * @return 0 on clean exit (user pressed 'q'), 1 on error.
     */
    template <typename KeyboardFactoryT = KeyboardFactory>
    static int run(
        const std::string& vehicleId,
        int intervalMs,
        std::ostream& out,
        vehicle_sim::util::IClock& clock,
        KeyboardFactoryT makeKeyboard = defaultKeyboard())
    {
        return runImpl(vehicleId, intervalMs, out, clock, std::move(makeKeyboard));
    }

private:
    static KeyboardFactory defaultKeyboard();

    /// Shared implementation. The factory is a template parameter (not
    /// std::function by value) so callers inject a test fake without
    /// type-erasure overhead or a Sonar finding (S5213/S1238).
    template <typename KeyboardFactoryT>
    static int runImpl(
        const std::string& vehicleId,
        int intervalMs,
        std::ostream& out,
        vehicle_sim::util::IClock& clock,
        KeyboardFactoryT makeKeyboard);
};

} // namespace vehicle_sim::cli
