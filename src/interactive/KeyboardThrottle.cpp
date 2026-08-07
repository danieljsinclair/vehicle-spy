// KeyboardThrottle.cpp - Keyboard-to-throttle mapper.
// Consumes the bridge's IKeyboardInput contract by injection; the key source
// is substitutable, so this maps keys to state without touching a terminal.

#include "vehicle-sim/interactive/KeyboardThrottle.h"

#include <algorithm>

#ifndef _WIN32
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace vehicle_sim::interactive {

namespace {
constexpr double GEAR_STEP = 1.0;        // gear is an integer 1..9
constexpr double STEER_STEP_DEG = 5.0;   // per arrow press
constexpr double STEER_LIMIT_DEG = 90.0;
constexpr int    GEAR_MIN = 1;
constexpr int    GEAR_MAX = 9;
} // namespace

KeyboardThrottle::KeyboardThrottle(std::unique_ptr<IKeyboardInput> keyboard)
    : m_keyboard{std::move(keyboard)}
{
#ifndef _WIN32
    setupTerminal();
#endif
}

KeyboardThrottle::~KeyboardThrottle() {
#ifndef _WIN32
    restoreTerminal();
#endif
}

KeyboardThrottle::State KeyboardThrottle::poll() {
    // Drain every pending keystroke, mutating the retained state. Throttle,
    // gear, brake and steering all LATCH: a key sets a value that persists
    // until the next key changes it (this is what makes the emitted CSV a
    // plausible "held pedal" stream for latency testing).
    int key;
    while ((key = getKey()) != -1) {
        if (key >= '1' && key <= '9') {
            m_state.throttle_percent = static_cast<double>(key - '0') * 10.0;
        } else if (key == '0') {
            m_state.throttle_percent = 100.0;
        } else if (key == 'q' || key == 'Q') {
            m_state.quit = true;
            return m_state;
        } else if (key == 'b' || key == 'B') {
            m_state.brake_percent = 100.0;
            m_state.throttle_percent = 0.0;   // braking releases the pedal
        } else if (key == 27) {
            // Arrow keys arrive as the escape sequence 27, '[', <letter>.
            const int b = getKey();
            const int c = getKey();
            if (b == '[') {
                applyArrow(c);
            }
        } else {
            // Any other key releases the brake (momentary brake model: brake
            // is held only while 'b' is the last key seen, released otherwise).
            m_state.brake_percent = 0.0;
        }
    }

    return m_state;
}

void KeyboardThrottle::applyArrow(int letter) {
    switch (letter) {
        case 'A':   // Up    → gear up
            m_state.gear = std::min(m_state.gear + static_cast<int>(GEAR_STEP), GEAR_MAX);
            break;
        case 'B':   // Down  → gear down
            m_state.gear = std::max(m_state.gear - static_cast<int>(GEAR_STEP), GEAR_MIN);
            break;
        case 'C':   // Right → steer +
            m_state.steering_angle_deg =
                std::min(m_state.steering_angle_deg + STEER_STEP_DEG, STEER_LIMIT_DEG);
            break;
        case 'D':   // Left  → steer -
            m_state.steering_angle_deg =
                std::max(m_state.steering_angle_deg - STEER_STEP_DEG, -STEER_LIMIT_DEG);
            break;
        default:
            break;
    }
}

int KeyboardThrottle::getKey() const {
    return m_keyboard ? m_keyboard->getKey() : -1;
}

#ifndef _WIN32

bool KeyboardThrottle::setupTerminal() {
    if (tcgetattr(STDIN_FILENO, &m_oldSettings) != 0) return false;

    termios newSettings = m_oldSettings;
    newSettings.c_lflag &= ~(ICANON | ECHO);
    newSettings.c_cc[VMIN]  = 0;
    newSettings.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &newSettings) != 0) return false;

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) return false;

    m_initialized = true;
    return true;
}

void KeyboardThrottle::restoreTerminal() const {
    if (m_initialized) {
        tcsetattr(STDIN_FILENO, TCSANOW, &m_oldSettings);
    }
}

#endif // _WIN32

} // namespace vehicle_sim::interactive
