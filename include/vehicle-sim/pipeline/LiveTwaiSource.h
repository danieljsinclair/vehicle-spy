#pragma once

#include "vehicle-sim/pipeline/IFrameSource.h"
#include "vehicle-sim/pipeline/IAdapterNormaliser.h"

#include <memory>

namespace vehicle_sim::pipeline {

class ITransport;

/**
 * Wraps a live ITransport (TCP/USB/demo) as an IFrameSource. The transport
 * delivers text lines ("<CANID> <D0> ... <D7>"); this source tokenises them
 * into the 10-byte TWAI shape the decoder consumes and stamps wall-clock
 * time on each frame.
 *
 * For ELM327 transports the optional normaliser is supplied (e.g. an
 * Elm327Normaliser), which validates the 11-bit ID bound and skips adapter
 * chatter. For raw transports the normaliser is null and the inline parser
 * is used.
 *
 * Future WiCan adapter: a WiCanSource implements the IFrameSource interface
 * directly (no transport/normaliser wrapping) and the rest of the pipeline
 * is unchanged. The composition root is the only place that knows which
 * source is wired in.
 */
class LiveTwaiSource final : public IFrameSource {
public:
    /** Raw mode: inline parser, no adapter-specific validation. */
    explicit LiveTwaiSource(ITransport& transport);
    /** ELM327 mode: route each line through the supplied normaliser. */
    LiveTwaiSource(ITransport& transport, IAdapterNormaliser* normaliser);

    bool open() noexcept override;
    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] std::optional<TwaiFrame> nextFrame() noexcept override;

private:
    ITransport& transport_;
    IAdapterNormaliser* normaliser_;  // nullptr → raw inline parser
};

} // namespace vehicle_sim::pipeline
