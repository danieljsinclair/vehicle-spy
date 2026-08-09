#pragma once

#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/domain/CaptureLog.h"
#include "vehicle-sim/util/IClock.h"

#include <cstdint>

namespace vehicle_sim::pipeline {

/**
 * Owns the paced-replay timing state machine for one replay run.
 *
 * SRP split (this class is the ORCHESTRATION half):
 *   - ReplayPacing is the POLICY: a pure, stateless classifier that answers
 *     "given a baseline and an elapsed time, is this frame skipped / due now /
 *     due later?". It is injected, not inherited, and stays untouched.
 *   - PacedFrameScheduler is the ORCHESTRATION: it owns the run-scoped state
 *     the policy deliberately does not (the replay start instant, whether a
 *     baseline has been anchored yet, and what that baseline is), reads the
 *     clock, and performs the wait.
 *
 * Keeping the policy injected preserves Open/Closed: a different pacing rule
 * is a different ReplayPacing, with no change here. Re-bundling the policy
 * into this class (or back into the replay loop) would recreate the SRP
 * violation this split exists to remove.
 *
 * The scheduler holds a NON-const IClock& because performing the wait mutates
 * the clock (sleepFor advances a FakeClock's virtual time). That is what lets
 * the replay loop drop the const_cast it previously needed.
 *
 * Not thread-safe and not reusable across runs: one instance per replay run,
 * driven by a single loop.
 */
class PacedFrameScheduler {
public:
    /** What the replay loop should do with a frame. */
    enum class Action {
        /** Emit the frame now (any required wait has already been performed). */
        Emit,
        /** Drop the frame: blank, or recorded before --start-from. */
        Skip,
    };

    /**
     * @param pacing Pacing policy (blank detection + skip/due classification).
     *               Copied by value: it is a small value-semantic policy and
     *               the scheduler must not outlive-alias a caller's temporary.
     * @param clock  Clock used to measure elapsed time and to perform waits.
     *               Must outlive this scheduler.
     */
    PacedFrameScheduler(ReplayPacing pacing, util::IClock& clock) noexcept
        : pacing_(pacing), clock_(clock), replayStart_(clock.now()) {}

    PacedFrameScheduler(const PacedFrameScheduler&) = delete;
    PacedFrameScheduler& operator=(const PacedFrameScheduler&) = delete;

    /**
     * Decide what to do with the next frame, WAITING inline if the frame is
     * scheduled in the future so that on return the caller may act at once.
     *
     * Sequencing (unchanged from the loop this replaces):
     *   1. A blank frame never anchors the baseline and is always skipped.
     *   2. The first non-blank frame anchors the baseline and surfaces
     *      immediately (its scheduled offset is zero).
     *   3. Later frames are classified against the baseline and elapsed time:
     *      a negative classification (before --start-from) skips; a positive
     *      one waits out the remaining time, then emits.
     *
     * @param frame the normalised frame under consideration.
     * @return Emit to surface the frame, Skip to drop it.
     */
    [[nodiscard]] Action consider(const domain::RawFrame& frame);

private:
    /** Wall-clock ms since the replay started, floored at zero. */
    [[nodiscard]] std::uint64_t elapsedMs() const;

    ReplayPacing pacing_;
    util::IClock& clock_;
    util::IClock::time_point replayStart_;
    bool baselineSet_ = false;
    std::uint64_t baselineTsMs_ = 0;
};

} // namespace vehicle_sim::pipeline
