#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"

#include <cstdint>

namespace vehicle_sim::pipeline {

/**
 * Adapter normaliser for LIVE raw adapter output: lines of the form
 *   "<ID> <D0> <D1> ... <D7>"   e.g. "118 3C 00 18 00 00 00 00"
 * whitespace-separated hex tokens, '\r'/'\n'-terminated by the transport.
 * There is NO timestamp prefix — live frames are timestamped on receipt (the
 * transport/decoder stamps them with wall-clock time at decode).
 *
 * First token is the CAN-ID (hex); the remaining 1..8 tokens are the data
 * bytes. Non-frame lines (prompts, status text, banners, blanks) are Skip;
 * frame-shaped lines that fail to decode are Malformed. It owns no protocol
 * state and knows nothing about DBC (Open/Closed).
 *
 * This is the normaliser the live raw TCP path uses (TCPTransport →
 * RawFrameNormaliser). Capture-file replay uses CaptureNormaliser; the ELM327
 * dialect (AT-init + '>' prompt) is a later task (#18).
 */
class RawFrameNormaliser final : public IAdapterNormaliser {
public:
    /**
     * Parse one live raw adapter line into a RawFrame with timestampMs = 0
     * (the caller/decoder stamps wall-clock time for live sources). Exposed
     * for unit testing the pure parser independently of the socket worker.
     */
    [[nodiscard]] static NormaliserResult parseLiveLine(const std::string& line) noexcept;

    [[nodiscard]] NormaliserResult normalise(const std::string& line) noexcept override;
};

} // namespace vehicle_sim::pipeline
