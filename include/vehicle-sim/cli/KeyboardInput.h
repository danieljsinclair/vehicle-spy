#pragma once

#include "vehicle-sim/cli/IKeyboardInput.h"

#ifndef _WIN32
#include <termios.h>
#endif

namespace vehicle_sim::cli {

/**
 * Terminal-backed keyboard input — non-blocking, raw-mode on POSIX.
 * Wraps termios + fcntl(O_NONBLOCK) so callers see a simple int getKey().
 * Ported verbatim from engine-sim-cli's KeyboardInput (shared logic, DRY).
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

} // namespace vehicle_sim::cli
