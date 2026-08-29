#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PacedFrameScheduler.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/util/IClock.h"

#include <chrono>
#include <cstdint>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::util;

namespace {

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
        return true;
    }

private:
    FakeClock fake_;
    std::vector<std::chrono::milliseconds> sleeps_;
};

TwaiFrame frameAt(std::uint64_t tsMs, std::uint8_t dlc = 8) {
    TwaiFrame f;
    f.timestampMs = tsMs;
    // dlc=0 means "no data"; the rest is zero. 1..8 dlc fills the first dlc
    // bytes with a marker so the frame is not blank.
    for (std::size_t i = 0; i < dlc; ++i) f.bytes[2 + i] = static_cast<std::uint8_t>(i + 1);
    return f;
}

TwaiFrame blankFrame() {
    // All zeros — blank by the new (timestamp==0 && data==0) definition.
    return TwaiFrame{};
}

} // namespace

TEST(PacedFrameSchedulerTest, FirstNonBlankFrameEmitsWithoutWaiting) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    EXPECT_EQ(scheduler.consider(frameAt(5000)), PacedFrameScheduler::Action::Emit);
    EXPECT_TRUE(clock.sleeps().empty())
        << "the baseline frame defines t=0 and must not induce a wait";
}

TEST(PacedFrameSchedulerTest, LaterFrameWaitsTheRecordedGapThenEmits) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    ASSERT_EQ(scheduler.consider(frameAt(1000)), PacedFrameScheduler::Action::Emit);
    EXPECT_EQ(scheduler.consider(frameAt(2500)), PacedFrameScheduler::Action::Emit);

    ASSERT_EQ(clock.sleeps().size(), 1u);
    EXPECT_EQ(clock.sleeps().front().count(), 1500);
}

TEST(PacedFrameSchedulerTest, BlankFrameIsSkippedAndDoesNotAnchorBaseline) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    EXPECT_EQ(scheduler.consider(blankFrame()), PacedFrameScheduler::Action::Skip);
    EXPECT_EQ(scheduler.consider(frameAt(3000)), PacedFrameScheduler::Action::Emit);
    EXPECT_TRUE(clock.sleeps().empty())
        << "a blank frame must not become the pacing baseline";
}

TEST(PacedFrameSchedulerTest, StartFromSkipsEarlyRowsIncludingTheFirst) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{/*startFromS=*/2.0}, clock);

    EXPECT_EQ(scheduler.consider(frameAt(500)), PacedFrameScheduler::Action::Skip);
    EXPECT_EQ(scheduler.consider(frameAt(1500)), PacedFrameScheduler::Action::Skip);
    EXPECT_EQ(scheduler.consider(frameAt(2000)), PacedFrameScheduler::Action::Emit);
}

TEST(PacedFrameSchedulerTest, OverdueFrameEmitsWithoutWaiting) {
    RecordingClock clock;
    PacedFrameScheduler scheduler(ReplayPacing{}, clock);

    ASSERT_EQ(scheduler.consider(frameAt(1000)), PacedFrameScheduler::Action::Emit);
    ASSERT_EQ(scheduler.consider(frameAt(6000)), PacedFrameScheduler::Action::Emit);
    const auto sleepsAfterCatchUp = clock.sleeps().size();

    EXPECT_EQ(scheduler.consider(frameAt(2000)), PacedFrameScheduler::Action::Emit);
    EXPECT_EQ(clock.sleeps().size(), sleepsAfterCatchUp);
}
