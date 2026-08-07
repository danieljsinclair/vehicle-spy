#pragma once

namespace vehicle_sim::cli {

/**
 * Abstract keyboard input source — allows test injection without touching
 * the terminal. Mirrors engine-sim-cli's IKeyboardInput contract so the
 * throttle/keyboard logic can be shared (DRY) between the two tools.
 */
class IKeyboardInput {
public:
    virtual ~IKeyboardInput() = default;

    /** Return the next key code, or -1 if no key is available. */
    virtual int getKey() = 0;
};

} // namespace vehicle_sim::cli
