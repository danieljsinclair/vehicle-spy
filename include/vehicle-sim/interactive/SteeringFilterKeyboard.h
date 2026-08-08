#pragma once

// Contract consumed from the engine-sim-bridge submodule (read-only upstream).
#include "input/IKeyboardInput.h"

#include <memory>

namespace vehicle_sim::interactive {

class CsvKeyActionTarget;

/**
 * Decorator over the real keyboard that pulls arrow-key escape sequences out
 * of the stream before the bridge sees them.
 *
 * Why this exists: arrows arrive as 27,'[',<letter>, and the bridge's
 * KeyboardInputProvider maps byte 27 to QUIT and bytes 'A'/'B' to throttle
 * ramp. Fed raw, a single Up-arrow press would end the session (verified
 * experimentally against the submodule). Since the bridge is read-only
 * upstream and models no steering at all, the sequence is filtered out here
 * and routed to CsvKeyActionTarget instead.
 *
 * Every other key passes through untouched, so the bridge remains the single
 * source of truth for throttle, gear, brake and quit mapping.
 */
class SteeringFilterKeyboard final : public ::IKeyboardInput {
public:
    /**
     * @param inner  The real key source (owned).
     * @param target Receives decoded arrow keys (non-owning; must outlive this).
     */
    SteeringFilterKeyboard(std::unique_ptr<::IKeyboardInput> inner,
                           CsvKeyActionTarget* target);

    /**
     * Next key the bridge should process, or -1 when none remain.
     *
     * Filtered bytes are never returned as -1 mid-stream: the bridge drains
     * with `while ((key = reader()) >= 0)`, so a -1 for a swallowed arrow byte
     * would abort the drain and strand the rest of the frame's input. This
     * skips past consumed bytes and returns the next real key instead.
     */
    int getKey() override;

private:
    std::unique_ptr<::IKeyboardInput> m_inner;
    CsvKeyActionTarget*               m_target;
    int                               m_pendingKey{-1};  // displaced by a replay
};

} // namespace vehicle_sim::interactive
