#pragma once

// The contract comes straight from the engine-sim-bridge submodule; there is
// no vehicle-sim copy of it any more.
#include "input/IKeyboardInput.h"

#ifndef _WIN32
#include <termios.h>
#endif

namespace vehicle_sim::interactive {

/**
 * Terminal-backed keyboard input — non-blocking, raw-mode on POSIX.
 * Wraps termios + fcntl(O_NONBLOCK) so callers see a simple int getKey().
 *
 * This is the one keyboard file that is legitimately vehicle-sim's own. The
 * bridge ships IKeyboardInput as a pure interface and provides NO concrete
 * reader (no termios anywhere in it), so every consumer supplies its own
 * terminal adapter — engine-sim-cli does exactly the same at
 * src/input/KeyboardInput.{h,cpp}, outside the submodule. Deleting this class
 * would leave KeyboardInputProvider unconstructible.
 *
 * Key MAPPING is emphatically not here: that all lives in the bridge.
 */
class KeyboardInput final : public ::IKeyboardInput {
public:
    KeyboardInput();
    ~KeyboardInput() override;

    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;
    KeyboardInput(KeyboardInput&&) = delete;
    KeyboardInput& operator=(KeyboardInput&&) = delete;

    int getKey() override;

private:
#ifndef _WIN32
    bool setupTerminal();

    termios m_oldSettings{};
    bool    m_initialized{false};
#endif
};

} // namespace vehicle_sim::interactive
