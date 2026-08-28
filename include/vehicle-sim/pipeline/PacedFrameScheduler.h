#pragma once

#include "vehicle-sim/pipeline/IFrameSource.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/util/IClock.h"

#include <cstdint>

namespace vehicle_sim::pipeline {

/**
 * Owns the paced-replay timing state machine for one replay run.
 *
 * SRP split (this class is the ORCHESTRATION half):
 *   - ReplayPacing is the POLICY: a pure, stateless classifier that answers
 *     "given a baseline and an elapsed time, is this frame skipped / due now /
 *     due later?".
 *   - PacedFrameScheduler is the ORCHESTRATION: it owns the run-scoped state
 *     (replay-start instant, whether a baseline has been anchored, and what
 *     that baseline is), reads the clock, and performs the wait.
 *
 * The scheduler holds a NON-const IClock& because performing the wait mutates
 * the clock (sleepFor advances a FakeClock's virtual time).
 *
 * Not thread-safe, not reusable across runs: one instance per replay run.
 */
class PacedFrameScheduler {
public:
    enum class Action {
        Emit,
        Skip,
    };

    PacedFrameScheduler(ReplayPacing pacing, util::IClock& clock) noexcept
        : pacing_(pacing), clock_(clock), replayStart_(clock.now()) {}

    PacedFrameScheduler(const PacedFrameScheduler&) = delete;
    PacedFrameScheduler& operator=(const PacedFrameScheduler&) = delete;

    /**
     * Decide what to do with the next frame, WAITING inline if the frame is
     * scheduled in the future. Sequencing:
     *   1. Blank frames never anchor the baseline; they are always skipped.
     *   2. The first non-blank frame anchors the baseline and surfaces
     *      immediately (scheduled offset = 0).
     *   3. Later frames are classified against baseline and elapsed: a
     *      negative classification (before --start-from) skips; a positive
     *      one waits out the remaining time, then emits.
     */
    [[nodiscard]] Action consider(const TwaiFrame& frame);

private:
    [[nodiscard]] std::uint64_t elapsedMs() const;

    ReplayPacing pacing_;
    util::IClock& clock_;
    util::IClock::time_point replayStart_;
    bool baselineSet_ = false;
    std::uint64_t baselineTsMs_ = 0;
};

} // namespace vehicle_sim::pipeline
