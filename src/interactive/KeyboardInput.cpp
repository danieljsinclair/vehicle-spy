// KeyboardInput.cpp - Non-blocking terminal keyboard input.
// Concrete POSIX terminal adapter for the bridge's IKeyboardInput contract.

#include "vehicle-sim/interactive/KeyboardInput.h"

#ifndef _WIN32

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace vehicle_sim::interactive {

KeyboardInput::KeyboardInput()
    : m_initialized{setupTerminal()}
{
}

KeyboardInput::~KeyboardInput() {
    if (m_initialized) {
        tcsetattr(STDIN_FILENO, TCSANOW, &m_oldSettings);
    }
}

int KeyboardInput::getKey() {
    if (m_initialized) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            return static_cast<unsigned char>(c);
        }
    }
    return -1;
}

bool KeyboardInput::setupTerminal() {
    if (tcgetattr(STDIN_FILENO, &m_oldSettings) != 0) return false;

    termios newSettings = m_oldSettings;
    newSettings.c_lflag &= ~(ICANON | ECHO);
    newSettings.c_cc[VMIN]  = 0;
    newSettings.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &newSettings) != 0) return false;

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) return false;

    return true;
}

} // namespace vehicle_sim::interactive

#endif // _WIN32
