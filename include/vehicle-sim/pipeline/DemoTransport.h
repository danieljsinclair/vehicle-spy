#pragma once

#include "vehicle-sim/pipeline/ITransport.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vehicle_sim::pipeline {

/**
 * Synthetic LIVE transport: emits a plausible driving loop as raw adapter
 * text lines (the same "<ID> <D0> ... <D7>" form a real raw CAN adapter
 * streams). The lines flow through RawFrameNormaliser → DBCTranslationService
 * → sinks exactly like a live TCP/USB source, so demo is exercised through
 * the canonical seam rather than synthesising VehicleSignals directly.
 *
 * The driving loop ramps speed 0..~120 kmh and back, holds gear D, and varies
 * throttle/torque proportionally. Frames are encoded to match the canonical
 * opendbc tesla_model3_party.dbc layout (CAN IDs 264/280/599) so a Tesla
 * vehicle decodes them into real VehicleSignals. Because demo emits the text
 * form, a non-Tesla vehicle simply won't decode the IDs (the pipeline Skip's
 * them) — no DBC knowledge lives here beyond the byte layout of the demo
 * frames, which is data, not a decode dependency.
 *
 * Bounded: the transport yields a finite number of frames then EOF (nullopt),
 * so the replay loop terminates for tests and short demo runs. The cycle count
 * is configurable; default produces a short, representative drive.
 */
class DemoTransport final : public ITransport {
public:
    /**
     * @param frameCount Number of synthetic frames to emit before EOF.
     *                   Default yields a short demo drive (~a few seconds
     *                   of wall-clock-equivalent telemetry).
     */
    explicit DemoTransport(std::size_t frameCount = kDefaultFrameCount);

    bool open() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    std::optional<std::string> nextLine() override;

    /** Default frame count for the demo driving loop. */
    static constexpr std::size_t kDefaultFrameCount = 600;

private:
    // Build the raw-adapter text line for frame index i of the driving loop.
    // Returns the n-th frame's "<ID> <D0> ... <D7>" string. Exposed for tests.
    [[nodiscard]] static std::string buildFrameLine(std::size_t i) noexcept;

    std::size_t total_;
    std::size_t emitted_ = 0;
    bool opened_ = false;
};

} // namespace vehicle_sim::pipeline
