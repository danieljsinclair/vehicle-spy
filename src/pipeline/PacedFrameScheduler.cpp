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
        recordingBaselineTsMs_ = frame.timestampMs;
        pacingBaselineTsMs_ = frame.timestampMs;
        baselineSet_ = true;
    }
    const std::int64_t waitMs = pacing_.classifyFrame(
        frame, recordingBaselineTsMs_, pacingBaselineTsMs_, elapsedMs());
    if (waitMs < 0) return Action::Skip;
    if (!emittedAny_) {
        // First frame past the skip gate: the offset point becomes the
        // pacing origin. Emitted NOW (no wait) — see the header's sequencing
        // note 4. With no skip configured this is the recording's first
        // frame, so both origins coincide and behavior is unchanged.
        pacingBaselineTsMs_ = frame.timestampMs;
        replayStart_ = clock_.now();
        emittedAny_ = true;
        return Action::Emit;
    }
    if (waitMs > 0) clock_.sleepFor(std::chrono::milliseconds(waitMs));
    return Action::Emit;
}

} // namespace vehicle_sim::pipeline
