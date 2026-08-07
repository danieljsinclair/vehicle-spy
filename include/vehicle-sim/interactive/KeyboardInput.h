#pragma once

#include "vehicle-sim/interactive/IKeyboardInput.h"

#ifndef _WIN32
#include <termios.h>
#endif

namespace vehicle_sim::interactive {

/**
 * Terminal-backed keyboard input — non-blocking, raw-mode on POSIX.
 * Wraps termios + fcntl(O_NONBLOCK) so callers see a simple int getKey().
 *
 * This is the one piece that is genuinely vehicle-sim's own: it is the
 * concrete POSIX terminal adapter implementing the bridge's IKeyboardInput
 * contract. The bridge supplies the interface; we supply the terminal.
 */
class KeyboardInput final : public IKeyboardInput {
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
