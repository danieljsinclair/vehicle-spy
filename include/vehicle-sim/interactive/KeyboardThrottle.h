#pragma once

#include "vehicle-sim/interactive/IKeyboardInput.h"

#include <memory>

#ifndef _WIN32
#include <termios.h>
#endif

namespace vehicle_sim::interactive {

/**
 * Keyboard-to-throttle mapper — shared between CSV replay and interactive
 * modes. Depends on the bridge's IKeyboardInput contract by injection
 * (see IKeyboardInput.h in this folder), so the key-reading source is
 * substitutable and the mapper is testable without a terminal.
 *
 * The mapper is STATEFUL: it accumulates throttle/gear/brake/steering across
 * polls so a key pressed once stays applied until changed or released. Each
 * poll() drains all pending keystrokes and returns the current state.
 *
 * Key mapping (mirrors engine-sim-cli 1-0 keys):
 *   '1'–'9'  → throttle 10%–90% (set, latched)
 *   '0'      → throttle 100% (set, latched)
 *   'q'/'Q'  → quit
 *   Up/Down arrow (27,'[','A'/'B') → gear up/down (clamped 1..9)
 *   Left/Right arrow (27,'[','D'/'C') → steering -5°/+5° (clamped ±90°)
 *   'b'/'B'  → brake 100% (latched); any other key releases brake to 0%
 */
class KeyboardThrottle {
public:
    struct State {
        double throttle_percent{0.0};   // 0..100, latched
        int    gear{1};                 // 1..9, 1 = first gear
        double brake_percent{0.0};      // 0 or 100, latched
        double steering_angle_deg{0.0}; // -90..90, accumulates
        bool   quit{false};
    };

    /**
     * @param keyboard Owned keyboard input provider (injected for testability).
     */
    explicit KeyboardThrottle(std::unique_ptr<IKeyboardInput> keyboard);

    ~KeyboardThrottle();

    KeyboardThrottle(const KeyboardThrottle&) = delete;
    KeyboardThrottle& operator=(const KeyboardThrottle&) = delete;
    KeyboardThrottle(KeyboardThrottle&&) = delete;
    KeyboardThrottle& operator=(KeyboardThrottle&&) = delete;

    /** Poll keyboard once; drain all pending keys; return current state. */
    State poll();

private:
    /** Apply an arrow-key command (the <letter> of the 27,'[',<letter> sequence). */
    void applyArrow(int letter);

    int getKey() const;

#ifndef _WIN32
    bool setupTerminal();
    void restoreTerminal() const;

    termios m_oldSettings{};
    bool    m_initialized{false};
#endif

    std::unique_ptr<IKeyboardInput> m_keyboard;
    State                           m_state;   // accumulated across polls
};

} // namespace vehicle_sim::interactive
