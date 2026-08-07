#pragma once

// Contracts consumed from the engine-sim-bridge submodule (read-only upstream).
#include "input/IKeyActionTarget.h"

namespace vehicle_sim::interactive {

/**
 * Callback target that turns bridge key actions into CSV telemetry values.
 *
 * The division of labour is deliberate and worth stating, because it is the
 * whole point of this class:
 *
 *   - The BRIDGE owns all keyboard mapping and timing. Which key means
 *     "70% throttle", how long a held key stays pinned before snapping back
 *     (KeyHoldBridge, 250ms initial / 50ms repeat) — none of that is
 *     reimplemented here. KeyboardInputProvider decodes keys and calls the
 *     methods below.
 *
 *   - This ADAPTER owns only the translation into vehicle telemetry, plus the
 *     one concept the bridge does not model at all: STEERING. The bridge
 *     simulates an engine, so it has no steering field on EngineInput and no
 *     steering method on IKeyActionTarget (verified: zero "steer" matches in
 *     its include/ and src/). vehicle-sim's CSV contract requires the
 *     steering_angle_deg column, so the arrow-key escape sequence is decoded
 *     here rather than pushed upstream into the bridge.
 *
 * IKeyActionTarget's methods are all defaulted no-ops, so we override strictly
 * what the CSV needs and inherit silence for the rest (ignition, starter,
 * dyno, presets — meaningless for an EV telemetry bench).
 */
class CsvKeyActionTarget final : public input::IKeyActionTarget {
public:
    /** Snapshot of the control state the CSV row is built from. */
    struct State {
        double throttle_percent{0.0};   // 0..100
        int    gear{1};                 // 1..9
        double brake_percent{0.0};      // 0 or 100
        double steering_angle_deg{0.0}; // -90..90, accumulates
        bool   quit{false};
    };

    // --- Bridge callbacks: throttle -----------------------------------------
    // The bridge fires setThrottleMomentary every frame a digit is held, and
    // stops firing on release — that is what produces snap-back. setThrottle
    // is the edge-triggered form (space = 0%, 'r' = 20%).
    void setThrottleMomentary(double fraction) override;
    void setThrottle(double fraction) override;
    void adjustThrottle(double delta) override;

    // --- Bridge callbacks: gear / brake / quit ------------------------------
    void shiftUp() override;
    void shiftDown() override;
    void setBrake(double fraction) override;
    void quit() override;

    /**
     * Feed one raw key code, for the arrow-key escape sequence only.
     *
     * Arrow keys arrive as the three bytes 27, '[', <letter>. The bridge has no
     * escape-sequence parser and treats byte 27 as QUIT, so arrows must be
     * intercepted before they reach it or Up-arrow would end the session.
     * Returns true when the byte was consumed as part of an escape sequence
     * (and so must NOT be forwarded to the bridge).
     */
    bool consumeEscapeSequence(int key);

    /**
     * Take a byte that was provisionally swallowed but turned out not to start
     * an arrow sequence (a bare ESC = quit), or -1 if there is none. Calling
     * this clears it, so each replayed byte is surfaced exactly once.
     */
    int takeReplayKey();

    /** Current accumulated control state. */
    const State& state() const { return m_state; }

private:
    /** Apply the <letter> of a decoded 27,'[',<letter> sequence. */
    void applyArrow(int letter);

    /** Where we are within an escape sequence: none -> ESC seen -> '[' seen. */
    enum class EscapeStage { None, Escape, Bracket };

    State       m_state;
    EscapeStage m_escapeStage{EscapeStage::None};
    int         m_replayKey{-1};   // byte to re-surface, or -1 for none
};

} // namespace vehicle_sim::interactive
