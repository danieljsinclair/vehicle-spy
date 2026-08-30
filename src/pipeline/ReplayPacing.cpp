#include "vehicle-sim/pipeline/ReplayPacing.h"

namespace vehicle_sim::pipeline {

std::int64_t ReplayPacing::classifyFrame(
    const TwaiFrame& frame,
    std::uint64_t recordingBaselineTsMs,
    std::uint64_t pacingBaselineTsMs,
    std::uint64_t elapsedSinceStartMs) const noexcept {

    if (startFromS_ >= 0.0) {
        // Skip gate: RELATIVE to the recording's first frame, and persistent
        // for the whole run (pre-window content that arrives late — an
        // out-of-order row — is still pre-window content). Real captures
        // carry epoch-scale timestamps (13-digit ms since 1970); comparing
        // the RAW timestamp against a relative threshold never skipped
        // anything on them (the skip only fired on synthetic captures
        // starting at 0). Wrap-around on out-of-order frames underflows to a
        // huge value, which falls through to the schedule math below and is
        // classified overdue — the same treatment pre-baseline frames get.
        const auto relMs = frame.timestampMs - recordingBaselineTsMs;
        const auto thresholdMs = static_cast<std::uint64_t>(startFromS_ * 1000.0);
        if (relMs < thresholdMs) {
            return -1;  // Skip
        }
    }

    // Pacing schedule: measured from the PACING origin — the first kept
    // frame when a skip window is active (the scheduler re-baselines at the
    // offset point so the window costs no wall time), otherwise the
    // recording's first frame.
    const std::int64_t scheduledMs =
        static_cast<std::int64_t>(frame.timestampMs) -
        static_cast<std::int64_t>(pacingBaselineTsMs);
    const std::int64_t behindMs =
        scheduledMs - static_cast<std::int64_t>(elapsedSinceStartMs);

    if (behindMs <= 0) return 0;  // Surface now
    return behindMs;  // Future: caller sleeps
}

} // namespace vehicle_sim::pipeline
