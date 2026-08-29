#pragma once

#include "vehicle-sim/pipeline/IFrameSource.h"
#include <cstdint>

namespace vehicle_sim::pipeline {

/**
 * Replay pacing + skip policy for the replay path. Pure policy (no state)
 * — PacedFrameScheduler owns the orchestration state.
 *
 * Notes (preserved from the prior RawFrame version):
 *   - Mirror of engine-sim-cli's LiveTelemetryProvider setStartFromS /
 *     classifyRow / isSampleBlank / tryReadNextRowPaced. Do NOT DRY this
 *     across repos.
 *   - The blank-sentinel in the legacy raw-CAN path was "timestamp==0 AND
 *     payload-length==0" (a placeholder row carrying no telemetry). The
 *     IFrameSource pipeline emits valid 10-byte TWAI frames only, so the
 *     blank check is now "timestamp==0 AND all data bytes are zero" — the
 *     equivalent zero-telemetry signature on the new frame shape.
 */
class ReplayPacing {
public:
    explicit ReplayPacing(double startFromS = -1.0) noexcept
        : startFromS_(startFromS) {}

    void setStartFromS(double s) noexcept { startFromS_ = s; }
    [[nodiscard]] double startFromS() const noexcept { return startFromS_; }

    /** True when the frame carries no telemetry (zero timestamp AND no data). */
    [[nodiscard]] static bool isFrameBlank(const TwaiFrame& frame) noexcept {
        if (frame.timestampMs != 0) return false;
        for (std::size_t i = 2; i < 10; ++i) {
            if (frame.bytes[i] != 0) return false;
        }
        return true;
    }

    /**
     * @return -1 => Skip (before --start-from, or a blank frame).
     *          0 => Surface now.
     *         >0 => Future: caller must wait this many ms.
     */
    [[nodiscard]] std::int64_t classifyFrame(
        const TwaiFrame& frame,
        std::uint64_t baselineTsMs,
        std::uint64_t elapsedSinceStartMs) const noexcept;

private:
    double startFromS_;
};

} // namespace vehicle_sim::pipeline
