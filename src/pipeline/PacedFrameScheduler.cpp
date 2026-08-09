// PacedFrameScheduler.cpp - orchestration half of the paced-replay split.
// See PacedFrameScheduler.h for the policy/orchestration rationale.

#include "vehicle-sim/pipeline/PacedFrameScheduler.h"

#include <chrono>

namespace vehicle_sim::pipeline {

std::uint64_t PacedFrameScheduler::elapsedMs() const {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_.now() - replayStart_).count();
    // A non-monotonic or not-yet-advanced clock can read slightly negative;
    // floor at zero so the policy always sees a sane elapsed time.
    return static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed);
}

PacedFrameScheduler::Action PacedFrameScheduler::consider(
    const domain::RawFrame& frame) {

    // Blank rows carry no telemetry: they never anchor the baseline and are
    // always dropped in replay (the live path records everything).
    if (ReplayPacing::isFrameBlank(frame)) {
        return Action::Skip;
    }

    // The first non-blank frame anchors the baseline, then is classified like
    // any other. It must NOT short-circuit to Emit: --start-from is evaluated
    // inside classifyFrame, so a first frame recorded before the requested
    // start still has to be skipped.
    if (!baselineSet_) {
        baselineTsMs_ = frame.timestampMs;
        baselineSet_ = true;
    }

    const std::int64_t waitMs =
        pacing_.classifyFrame(frame, baselineTsMs_, elapsedMs());

    // Negative => recorded before --start-from: drop it.
    if (waitMs < 0) {
        return Action::Skip;
    }

    // Positive => scheduled in the future: wait it out so the caller can emit
    // immediately on return. Zero means "due now" and needs no wait.
    if (waitMs > 0) {
        clock_.sleepFor(std::chrono::milliseconds(waitMs));
    }

    return Action::Emit;
}

} // namespace vehicle_sim::pipeline
