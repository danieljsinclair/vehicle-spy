#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * A single decoded TWAI CAN frame, ready to feed to DBCTranslationService.
 *
 * The 10-byte wire format is fixed: [canId_lo, canId_hi, d0..d7]. The timestamp
 * is the capture's recorded wall-clock UTC millisecond (epoch ms); the decoder
 * forwards it on to the emitted VehicleSignal.
 *
 * rawLine (optional, empty = absent) carries the VERBATIM transport line the
 * frame was parsed from (the bytes exactly as delivered, before parsing). The
 * replay loop mirrors it to RawLogSink so a live run captures its own raw
 * stream. Sources with no text form (BinaryFileSource — the input file already
 * IS the source of truth) leave it empty, which is why file replay writes only
 * the decoded CSV.
 *
 * This is the canonical pre-DBC frame shape for BOTH live and file replay
 * paths. The seam (IFrameSource) is what makes a future WiCan adapter a
 * drop-in: a WiCanSource implements open()/isOpen()/nextFrame() and the rest
 * of the pipeline is unchanged.
 */
struct TwaiFrame {
    std::uint64_t timestampMs = 0;
    std::array<std::uint8_t, 10> bytes{};
    std::string rawLine;  // verbatim transport line; empty = not captured
};

/**
 * Pull-based source of TWAI frames. The single seam between "where bytes come
 * from" and "what the pipeline does with them". Live and file replay differ
 * ONLY in which IFrameSource implementation is wired in the composition root
 * (main.cpp / Run contexts) — runReplay is transport-agnostic.
 *
 * Design intent: a future WiCan CAN-adapter source implements this same
 * interface and the rest of the pipeline (decoder, sinks, pacing) is
 * unchanged. The composition root is the ONLY place that knows which source
 * is in use.
 */
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    IFrameSource() = default;
    IFrameSource(const IFrameSource&) = delete;
    IFrameSource& operator=(const IFrameSource&) = delete;
    IFrameSource(IFrameSource&&) = delete;
    IFrameSource& operator=(IFrameSource&&) = delete;

    /** Open the underlying source. Returns false on open failure. */
    virtual bool open() noexcept = 0;

    /** True iff open() succeeded and the source is not yet exhausted. */
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /**
     * Fetch the next frame. Returns nullopt at EOF / disconnect / on a
     * non-frame transport line (header, status, blank, malformed). A
     * transport line that fails to decode is silently skipped — the caller
     * does not see it.
     */
    [[nodiscard]] virtual std::optional<TwaiFrame> nextFrame() noexcept = 0;
};

} // namespace vehicle_sim::pipeline
