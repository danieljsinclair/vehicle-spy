#pragma once

#include "vehicle-sim/pipeline/IFrameSource.h"

#include <optional>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Outcome of normalising one transport line.
 *   - Frame   : a valid TwaiFrame was produced — feed it to the decoder.
 *   - Skip    : not a frame and not an error (header, status text, blank).
 *   - Malformed: looks like a frame but failed to decode.
 */
enum class NormaliserResultKind {
    Frame,
    Skip,
    Malformed,
};

struct NormaliserResult {
    NormaliserResultKind kind = NormaliserResultKind::Skip;
    TwaiFrame frame;  // valid only when kind == Frame

    static NormaliserResult skip() {
        NormaliserResult r; r.kind = NormaliserResultKind::Skip; return r;
    }
    static NormaliserResult malformed() {
        NormaliserResult r; r.kind = NormaliserResultKind::Malformed; return r;
    }
    static NormaliserResult ofFrame(TwaiFrame f) {
        NormaliserResult r; r.kind = NormaliserResultKind::Frame; r.frame = std::move(f); return r;
    }
};

/**
 * Translate a transport line into a TwaiFrame. Knows the adapter's framing
 * (ELM327 monitor dialect, ...). Must NOT know about DBC or which transport
 * delivered the line (Open/Closed).
 */
class IAdapterNormaliser {
public:
    virtual ~IAdapterNormaliser() = default;
    IAdapterNormaliser() = default;
    IAdapterNormaliser(const IAdapterNormaliser&) = delete;
    IAdapterNormaliser& operator=(const IAdapterNormaliser&) = delete;
    IAdapterNormaliser(IAdapterNormaliser&&) = delete;
    IAdapterNormaliser& operator=(IAdapterNormaliser&&) = delete;

    [[nodiscard]] virtual NormaliserResult normalise(const std::string& line) noexcept = 0;
};

} // namespace vehicle_sim::pipeline
