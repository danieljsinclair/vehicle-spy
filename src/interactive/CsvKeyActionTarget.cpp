// CsvKeyActionTarget.cpp - Bridge key actions -> CSV telemetry values.
// Contains NO key mapping: the bridge decides which key means what. The only
// local decoding is the arrow-key escape sequence, because the bridge models
// no steering and would swallow byte 27 as QUIT.

#include "vehicle-sim/interactive/CsvKeyActionTarget.h"

#include <algorithm>

namespace vehicle_sim::interactive {

namespace {
constexpr double STEER_STEP_DEG  = 5.0;   // per arrow press
constexpr double STEER_LIMIT_DEG = 90.0;
constexpr int    GEAR_MIN = 1;
constexpr int    GEAR_MAX = 9;
constexpr double PERCENT  = 100.0;

constexpr int KEY_ESCAPE  = 27;
constexpr int KEY_BRACKET = '[';
} // namespace

void CsvKeyActionTarget::setThrottleMomentary(double fraction) {
    m_state.throttle_percent = std::clamp(fraction, 0.0, 1.0) * PERCENT;
}

void CsvKeyActionTarget::setThrottle(double fraction) {
    m_state.throttle_percent = std::clamp(fraction, 0.0, 1.0) * PERCENT;
}

void CsvKeyActionTarget::adjustThrottle(double delta) {
    m_state.throttle_percent =
        std::clamp(m_state.throttle_percent + delta * PERCENT, 0.0, PERCENT);
}

void CsvKeyActionTarget::shiftUp() {
    m_state.gear = std::min(m_state.gear + 1, GEAR_MAX);
}

void CsvKeyActionTarget::shiftDown() {
    m_state.gear = std::max(m_state.gear - 1, GEAR_MIN);
}

void CsvKeyActionTarget::setBrake(double fraction) {
    m_state.brake_percent = std::clamp(fraction, 0.0, 1.0) * PERCENT;
    if (m_state.brake_percent > 0.0) {
        m_state.throttle_percent = 0.0;   // braking releases the pedal
    }
}

void CsvKeyActionTarget::quit() {
    m_state.quit = true;
}

bool CsvKeyActionTarget::consumeEscapeSequence(int key) {
    bool consumed = true;

    switch (m_escapeStage) {
        case EscapeStage::None:
            if (key == KEY_ESCAPE) {
                m_escapeStage = EscapeStage::Escape;
            } else {
                consumed = false;   // ordinary key: let the bridge map it
            }
            break;

        case EscapeStage::Escape:
            if (key == KEY_BRACKET) {
                m_escapeStage = EscapeStage::Bracket;
            } else {
                // Not an arrow after all: the ESC we swallowed was a real quit
                // keypress. Queue it for replay so the bridge still sees it —
                // otherwise ESC would be silently eaten and quit would break.
                m_escapeStage = EscapeStage::None;
                m_replayKey   = KEY_ESCAPE;
                consumed      = false;
            }
            break;

        case EscapeStage::Bracket:
            applyArrow(key);
            m_escapeStage = EscapeStage::None;
            break;
    }

    return consumed;
}

int CsvKeyActionTarget::takeReplayKey() {
    const int key = m_replayKey;
    m_replayKey = -1;
    return key;
}

void CsvKeyActionTarget::applyArrow(int letter) {
    switch (letter) {
        case 'A':   // Up    -> gear up
            shiftUp();
            break;
        case 'B':   // Down  -> gear down
            shiftDown();
            break;
        case 'C':   // Right -> steer +
            m_state.steering_angle_deg =
                std::min(m_state.steering_angle_deg + STEER_STEP_DEG, STEER_LIMIT_DEG);
            break;
        case 'D':   // Left  -> steer -
            m_state.steering_angle_deg =
                std::max(m_state.steering_angle_deg - STEER_STEP_DEG, -STEER_LIMIT_DEG);
            break;
        default:
            break;
    }
}

} // namespace vehicle_sim::interactive
