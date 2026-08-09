#pragma once

#include "vehicle-sim/domain/CaptureLog.h"
#include <cstdint>

namespace vehicle_sim::pipeline {

/**
 * Replay pacing + skip policy for the raw-CAN replay path (PipelineReplay).
 *
 * This is a WET, intentionally-duplicated mirror of engine-sim-cli's
 * input/LiveTelemetryProvider (setStartFromS / classifyRow / isSampleBlank /
 * tryReadNextRowPaced). It exists so vehicle-sim's replay can later be swapped
 * for the shared library WITHOUT a refactor — do NOT DRY this into the
 * engine-sim-cli copy, and do NOT extract a cross-repo shared helper.
 *
 * Differences from the engine-sim-cli source it mirrors:
 *   - engine-sim-cli classifies a *decoded* CsvSample (throttle/roadSpeed) and
 *     paces by a SIM clock advanced by dt. Here we classify a *raw* RawFrame
 *     and pace against a WALL clock (steady_clock via IClock), because the
 *     replay driver emits immediately and honours the file's recorded
 *     timestamps rather than a simulated time.
 *   - The engine-sim-cli blank sentinel is `throttle==0 && roadSpeedKmh==-2`
 *     (a decoded-telemetry CSV concept). The raw-CAN layer has no throttle or
 *     roadSpeed fields, so the blank analogue here is a frame whose timestamp
 *     is zero AND whose payload length is zero — the signature of a bus-*
 *     wakeup / placeholder row carrying no telemetry. This is the raw-CAN
 *     equivalent of "skip blank/0-reading lines".
 */
class ReplayPacing {
public:
    explicit ReplayPacing(double startFromS = -1.0) noexcept
        : startFromS_(startFromS) {}

    /** Mirror of engine-sim-cli `setStartFromS`. -1.0 means "not set". */
    void setStartFromS(double s) noexcept { startFromS_ = s; }
    [[nodiscard]] double startFromS() const noexcept { return startFromS_; }

    /**
     * Raw-CAN analogue of engine-sim-cli `isSampleBlank`: true when the frame
     * carries no telemetry (zero timestamp AND zero-length payload). Such rows
     * are skipped in replay so they don't pollute the paced emission.
     */
    [[nodiscard]] static bool isFrameBlank(const domain::RawFrame& frame) noexcept {
        const bool noTimestamp = (frame.timestampMs == 0);
        const bool noPayload = (frame.dlc == 0);
        return noTimestamp && noPayload;
    }

    /**
     * Classify a frame relative to the replay clock, mirroring engine-sim-cli's
     * classifyRow (Skip / Surface / Future) but against wall-clock pacing.
     *
     * @param frame               The normalised raw frame.
     * @param baselineTsMs         Recorded timestamp (ms) of the first paced row
     *                            (the baseline the caller anchors on the first
     *                            non-blank frame). Caller owns the baseline.
     * @param elapsedSinceStartMs  Wall-clock ms elapsed since replay start
     *                            (derived from IClock::now() - replayStart).
     * @return -1  => Skip (before --start-from, or a blank frame).
     *          0  => Surface now (this row's scheduled time has arrived).
     *         >0  => Future: caller must wait this many milliseconds before
     *                surfacing (the row is ahead of the replay clock).
     */
    [[nodiscard]] std::int64_t classifyFrame(
        const domain::RawFrame& frame,
        std::uint64_t baselineTsMs,
        std::uint64_t elapsedSinceStartMs) const noexcept;

private:
    double startFromS_;
};

} // namespace vehicle_sim::pipeline
