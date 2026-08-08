// SteeringFilterKeyboard.cpp - Strip arrow-key escape sequences from the key
// stream before the bridge provider sees them.

#include "vehicle-sim/interactive/SteeringFilterKeyboard.h"
#include "vehicle-sim/interactive/CsvKeyActionTarget.h"

#include <utility>

namespace vehicle_sim::interactive {

SteeringFilterKeyboard::SteeringFilterKeyboard(
    std::unique_ptr<::IKeyboardInput> inner,
    CsvKeyActionTarget* target)
    : m_inner{std::move(inner)}
    , m_target{target}
{
}

int SteeringFilterKeyboard::getKey() {
    int key = -1;

    // A byte displaced by a replayed ESC is handed over before reading more.
    bool searching = true;
    if (m_pendingKey >= 0) {
        key = m_pendingKey;
        m_pendingKey = -1;
        searching = false;
    }

    // Skip over bytes belonging to an arrow-key sequence and surface the first
    // key the bridge should actually handle. A single loop keeps the "drain
    // until -1" contract intact for the caller.
    while (searching) {
        key = m_inner ? m_inner->getKey() : -1;

        if (key < 0) {
            searching = false;                       // stream exhausted
        } else if (!m_target) {
            searching = false;                       // no target: pass through
        } else if (!m_target->consumeEscapeSequence(key)) {
            // Not (part of) an arrow sequence. If a provisionally swallowed
            // ESC is pending, surface that first — it is the earlier keypress,
            // and the current byte will come back round on the next call.
            if (auto replay = m_target->takeReplayKey(); replay >= 0) {
                m_pendingKey = key;
                key = replay;
            }
            searching = false;
        }
        // else: byte was part of an escape sequence — keep looking.
    }

    return key;
}

} // namespace vehicle_sim::interactive
