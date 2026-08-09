// ReplayPacing.cpp - WET mirror of engine-sim-cli's LiveTelemetryProvider
// pacing/skip logic, adapted for the raw-CAN replay path. See ReplayPacing.h
// for the rationale and the documented deviations from the source it mirrors.

#include "vehicle-sim/pipeline/ReplayPacing.h"

namespace vehicle_sim::pipeline {

std::int64_t ReplayPacing::classifyFrame(
    const domain::RawFrame& frame,
    std::uint64_t baselineTsMs,
    std::uint64_t elapsedSinceStartMs) const noexcept {

    // --start-from analogue (mirrors engine-sim-cli: skip rows whose recorded
    // timestamp is before the requested start). Threshold is in recording-ms;
    // -1.0 means unset and skips nothing.
    if (startFromS_ >= 0.0) {
        const auto thresholdMs =
            static_cast<std::uint64_t>(startFromS_ * 1000.0);
        if (frame.timestampMs < thresholdMs) {
            return -1;  // Skip
        }
    }

    // Scheduled offset of this row relative to the first row's timestamp. The
    // first row anchors baselineTsMs so its scheduled offset is 0.
    const std::int64_t scheduledMs =
        static_cast<std::int64_t>(frame.timestampMs) -
        static_cast<std::int64_t>(baselineTsMs);

    // How far the replay clock is behind this row's scheduled time.
    const std::int64_t behindMs =
        scheduledMs - static_cast<std::int64_t>(elapsedSinceStartMs);

    if (behindMs <= 0) {
        return 0;  // Surface now (scheduled time already reached).
    }
    return behindMs;  // Future: caller sleeps this many ms.
}

} // namespace vehicle_sim::pipeline
