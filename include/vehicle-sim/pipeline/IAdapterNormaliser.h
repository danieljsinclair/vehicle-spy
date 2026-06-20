#pragma once

#include "vehicle-sim/domain/CaptureLog.h"
#include <optional>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Outcome of normalising one transport line.
 *   - Frame   : a valid RawFrame was produced — feed it to the decoder.
 *   - Skip    : not a frame and not an error (header, status text, blank,
 *               escaped noise). Callers ignore it silently. This mirrors
 *               CaptureParseResult::NotAFrame.
 *   - Malformed: looks like a frame but failed to decode. Callers may count
 *               it as skipped. This mirrors CaptureParseResult::Malformed.
 *
 * Using a distinct enum (rather than reusing CaptureParseResult) keeps the
 * pipeline seam decoupled from the CaptureLog header so later normalisers
 * (ELM327, which has its own prompt/AT-init states) can extend the vocabulary
 * without dragging CAN-capture concepts into every implementation.
 */
enum class NormaliserResultKind {
    Frame,
    Skip,
    Malformed,
};

struct NormaliserResult {
    NormaliserResultKind kind = NormaliserResultKind::Skip;
    domain::RawFrame frame;  // valid only when kind == Frame
    bool hasTimestamp = false;  // true when frame.timestampMs is meaningful

    static NormaliserResult skip() { return {NormaliserResultKind::Skip, {}, false}; }
    static NormaliserResult malformed() { return {NormaliserResultKind::Malformed, {}, false}; }
    static NormaliserResult ofFrame(domain::RawFrame f, bool timestamped = false) {
        return {NormaliserResultKind::Frame, std::move(f), timestamped};
    }
};

/**
 * Translate transport lines into RawFrame events. The normaliser is the
 * protocol-specific boundary: it knows how an adapter frames a CAN message
 * (ELM327 monitor "ID D0 D1...", legacy CSV "ts,id,dlc,hex", ...). It must
 * NOT know about DBC decode, and it must NOT know which transport delivered
 * the line (Open/Closed).
 *
 * Phase 1 implementation: RawFrameNormaliser (reuses domain::parseCaptureLine
 * for all the capture forms already supported). Phase 2 will add
 * ELM327Normaliser (AT-init + '>' prompt + monitor parsing) behind this same
 * interface.
 */
class IAdapterNormaliser {
public:
    virtual ~IAdapterNormaliser() = default;

    IAdapterNormaliser() = default;
    IAdapterNormaliser(const IAdapterNormaliser&) = delete;
    IAdapterNormaliser& operator=(const IAdapterNormaliser&) = delete;
    IAdapterNormaliser(IAdapterNormaliser&&) = delete;
    IAdapterNormaliser& operator=(IAdapterNormaliser&&) = delete;

    /**
     * Normalise one transport line. Idempotent and state-free for the Phase 1
     * raw normaliser; the ELM327 normaliser added later may carry per-adapter
     * session state (init sequence, prompt tracking) — hence a class, not a
     * free function.
     */
    [[nodiscard]] virtual NormaliserResult normalise(const std::string& line) noexcept = 0;
};

} // namespace vehicle_sim::pipeline
