#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PacedFrameScheduler.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/util/IClock.h"

#include <chrono>
#include <cstdint>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::util;

namespace {

// Records every sleepFor while keeping virtual time deterministic, so the
// scheduler's WAIT behaviour is observable without wall-clock time.
class RecordingClock final : public IClock {
public:
    [[nodiscard]] time_point now() const override { return fake_.now(); }
    void sleepFor(std::chrono::milliseconds d) override {
        sleeps_.push_back(d);
        fake_.sleepFor(d);
    }
    [[nodiscard]] const std::vector<std::chrono::milliseconds>& sleeps() const {
        return sleeps_;
    }

protected:
    [[nodiscard]] bool waitForImpl(std::condition_variable&,
                                   std::unique_lock<std::mutex>&,
                                   const std::function<bool()>&,
                                   time_point) const override {
        return true;  // never parked in these tests
    }

private:
    FakeClock fake_;
    std::vector<std::chrono::milliseconds> sleeps_;
};

RawFrame frameAt(std::uint64_t tsMs, std::uint8_t dlc = 8) {
    RawFrame f;
    f.timestampMs = tsMs;
    f.dlc = dlc;
    return f;
}

RawFrame blankFrame() {
    RawFrame f;
    f.timestampMs = 0;
    f.dlc = 0;
    return f;
}

} // namespace

// PacedFrameScheduler owns the replay timing state machine (baseline anchoring,
// elapsed-time reads, waiting) while delegating the skip/due classification to
// the injected ReplayPacing policy. These tests drive the scheduler directly,
// which the runReplay integration tests cannot do without a capture file.

// Contract: the first non-blank frame anchors the baseline and, with no
// --start-from set, is due immediately — its scheduled offset is zero, so no
// wait is performed.
TEST(PacedFrameSchedulerTest, FirstNonBlankFrameEmitsWithoutWaiting) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    EXPECT_EQ(scheduler.consider(frameAt(5000)), PacedFrameScheduler::Action::Emit);
    EXPECT_TRUE(clock.sleeps().empty())
        << "the baseline frame defines t=0 and must not induce a wait";
}

// Contract: a later frame waits out the gap between its recorded timestamp and
// the baseline before being emitted. The wait is performed INSIDE consider(),
// so the caller may emit as soon as it returns.
TEST(PacedFrameSchedulerTest, LaterFrameWaitsTheRecordedGapThenEmits) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    ASSERT_EQ(scheduler.consider(frameAt(1000)), PacedFrameScheduler::Action::Emit);
    EXPECT_EQ(scheduler.consider(frameAt(2500)), PacedFrameScheduler::Action::Emit);

    ASSERT_EQ(clock.sleeps().size(), 1u);
    EXPECT_EQ(clock.sleeps().front().count(), 1500)
        << "the wait should span the 1000ms->2500ms recorded gap";
}

// Contract: blank frames are dropped and, crucially, do NOT anchor the
// baseline. A blank seen first must leave the next real frame free to become
// the baseline (and therefore emit without a wait).
TEST(PacedFrameSchedulerTest, BlankFrameIsSkippedAndDoesNotAnchorBaseline) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    EXPECT_EQ(scheduler.consider(blankFrame()), PacedFrameScheduler::Action::Skip);

    // If the blank had anchored the baseline at 0ms, this frame would be 3000ms
    // in the future and would induce a wait.
    EXPECT_EQ(scheduler.consider(frameAt(3000)), PacedFrameScheduler::Action::Emit);
    EXPECT_TRUE(clock.sleeps().empty())
        << "a blank frame must not become the pacing baseline";
}

// Contract: --start-from skips rows recorded before the threshold, INCLUDING
// the very first non-blank frame. The first frame anchors the baseline but is
// still classified, so it must not short-circuit past the start-from check.
TEST(PacedFrameSchedulerTest, StartFromSkipsEarlyRowsIncludingTheFirst) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{/*startFromS=*/2.0}, clock);

    EXPECT_EQ(scheduler.consider(frameAt(500)), PacedFrameScheduler::Action::Skip)
        << "the first frame is not exempt from --start-from";
    EXPECT_EQ(scheduler.consider(frameAt(1500)), PacedFrameScheduler::Action::Skip);
    EXPECT_EQ(scheduler.consider(frameAt(2000)), PacedFrameScheduler::Action::Emit)
        << "a frame at the threshold is due";
}

// Contract: a frame whose scheduled time has already passed is emitted
// immediately rather than waiting. Replay never sleeps to "catch down" to a
// row it is already late for.
TEST(PacedFrameSchedulerTest, OverdueFrameEmitsWithoutWaiting) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    ASSERT_EQ(scheduler.consider(frameAt(1000)), PacedFrameScheduler::Action::Emit);

    // Burn 5000ms of virtual time by waiting out a far-future frame, so the
    // next frame is comfortably overdue.
    ASSERT_EQ(scheduler.consider(frameAt(6000)), PacedFrameScheduler::Action::Emit);
    const auto sleepsAfterCatchUp = clock.sleeps().size();

    EXPECT_EQ(scheduler.consider(frameAt(2000)), PacedFrameScheduler::Action::Emit);
    EXPECT_EQ(clock.sleeps().size(), sleepsAfterCatchUp)
        << "an already-overdue frame must not induce a further wait";
}
