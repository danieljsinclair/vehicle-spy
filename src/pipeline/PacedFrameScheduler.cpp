#include "vehicle-sim/pipeline/PacedFrameScheduler.h"

#include <chrono>

namespace vehicle_sim::pipeline {

std::uint64_t PacedFrameScheduler::elapsedMs() const {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_.now() - replayStart_).count();
    return static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed);
}

PacedFrameScheduler::Action PacedFrameScheduler::consider(const TwaiFrame& frame) {
    if (ReplayPacing::isFrameBlank(frame)) {
        return Action::Skip;
    }
    if (!baselineSet_) {
        baselineTsMs_ = frame.timestampMs;
        baselineSet_ = true;
    }
    const std::int64_t waitMs = pacing_.classifyFrame(frame, baselineTsMs_, elapsedMs());
    if (waitMs < 0) return Action::Skip;
    if (waitMs > 0) clock_.sleepFor(std::chrono::milliseconds(waitMs));
    return Action::Emit;
}

} // namespace vehicle_sim::pipeline
