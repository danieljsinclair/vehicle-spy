#include "vehicle-sim/pipeline/ReplayPacing.h"

namespace vehicle_sim::pipeline {

std::int64_t ReplayPacing::classifyFrame(
    const TwaiFrame& frame,
    std::uint64_t baselineTsMs,
    std::uint64_t elapsedSinceStartMs) const noexcept {

    if (startFromS_ >= 0.0) {
        const auto thresholdMs = static_cast<std::uint64_t>(startFromS_ * 1000.0);
        if (frame.timestampMs < thresholdMs) {
            return -1;  // Skip
        }
    }

    const std::int64_t scheduledMs =
        static_cast<std::int64_t>(frame.timestampMs) -
        static_cast<std::int64_t>(baselineTsMs);
    const std::int64_t behindMs =
        scheduledMs - static_cast<std::int64_t>(elapsedSinceStartMs);

    if (behindMs <= 0) return 0;  // Surface now
    return behindMs;  // Future: caller sleeps
}

} // namespace vehicle_sim::pipeline
