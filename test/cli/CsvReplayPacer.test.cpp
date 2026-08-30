#include "vehicle-sim/cli/CsvReplayPacer.h"
#include "vehicle-sim/util/IClock.h"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using vehicle_sim::cli::CsvReplayPacer;
using vehicle_sim::util::FakeClock;
using vehicle_sim::util::IClock;

namespace {

// Virtual clock: sleepFor records the requested wait AND advances virtual
// time (FakeClock semantics), so tests assert the schedule math — which
// waits were computed, against virtual elapsed time — never wall time.
// bump() lets a test make time pass WITHOUT a sleep, standing in for the
// read/encode/flush work the real loop does between rows.
class VirtualClock final : public IClock {
public:
    [[nodiscard]] time_point now() const override { return fake_.now(); }

    void sleepFor(std::chrono::milliseconds d) override {
        sleeps_.push_back(d.count());
        fake_.sleepFor(d);
    }

    void bump(std::chrono::milliseconds d) { fake_.advance(d); }

    [[nodiscard]] const std::vector<long long>& sleeps() const {
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
    std::vector<long long> sleeps_;
};

}  // namespace

// The first row IS the schedule's t=0: it anchors the origin and must
// never induce a wait, so replay starts immediately.
TEST(CsvReplayPacerTest, FirstRowAnchorsScheduleWithoutWaiting) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/0, clock);

    pacer.paceRow(1755000000000ull);

    EXPECT_TRUE(clock.sleeps().empty());
}

// Timestamp-driven waits are offsets from the FIRST row's timestamp (the
// origin), not per-row deltas: a row exactly due emits with no wait call,
// and each wait is the full remaining distance to its own deadline.
TEST(CsvReplayPacerTest, TimestampModeWaitsOffsetFromOriginNotPerRowDelta) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/0, clock);

    pacer.paceRow(1000);  // anchor, t=0
    pacer.paceRow(1500);  // scheduled 500, elapsed 0   -> sleep 500 (now 500)
    pacer.paceRow(1500);  // scheduled 500, elapsed 500 -> exactly due, no sleep
    pacer.paceRow(1750);  // scheduled 750, elapsed 500 -> sleep 250 (now 750)

    EXPECT_EQ(clock.sleeps(), (std::vector<long long>{500, 250}));
}

// THE compounding pin. Rows recorded 100ms apart, but each row costs 30ms
// of non-sleep time (bump = read/encode/flush). A relative per-row loop
// would sleep the full 100ms every row and finish 60ms late (0.83x); the
// absolute schedule must trim each wait by the overhead actually accrued
// since the origin so the run lands exactly on the recorded span.
TEST(CsvReplayPacerTest, TimeSpentBetweenRowsShortensTheNextWait) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/0, clock);

    const auto origin = clock.now();
    pacer.paceRow(0);      // anchor
    clock.bump(std::chrono::milliseconds(30));
    pacer.paceRow(100);    // scheduled 100, elapsed 30  -> sleep 70  (now 100)
    clock.bump(std::chrono::milliseconds(30));
    pacer.paceRow(200);    // scheduled 200, elapsed 130 -> sleep 70  (now 200)

    EXPECT_EQ(clock.sleeps(), (std::vector<long long>{70, 70}));
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(
                  clock.now() - origin).count(),
              200)
        << "virtual time must end exactly at the recorded span — zero drift";
}

// A row is late only relative to its own deadline: a long gap before it is
// slept out, and the row after it owes only the remaining distance.
TEST(CsvReplayPacerTest, LaterRowOwesOnlyItsRemainingDistance) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/0, clock);

    pacer.paceRow(1000);   // anchor
    pacer.paceRow(6000);   // scheduled 5000, elapsed 0 -> sleep 5000
    pacer.paceRow(6200);   // scheduled 5200, elapsed 5000 -> sleep 200

    EXPECT_EQ(clock.sleeps(), (std::vector<long long>{5000, 200}));
}

// Overdue rows (deadline already passed — including out-of-order rows
// whose recorded timestamp predates the origin's progress) emit
// immediately: no sleep call, and never a negative/backwards wait.
TEST(CsvReplayPacerTest, OverdueRowEmitsImmediatelyWithoutSleepingBackwards) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/0, clock);

    pacer.paceRow(1000);   // anchor
    clock.bump(std::chrono::milliseconds(2000));  // wall is now 2s past origin
    pacer.paceRow(1500);   // scheduled 500 < elapsed 2000: overdue

    EXPECT_TRUE(clock.sleeps().empty());
}

// Explicit -i interval: a fixed grid measured from the origin, ignoring
// the recorded timestamps entirely.
TEST(CsvReplayPacerTest, ExplicitIntervalUsesAFixedGridFromTheOrigin) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/250, clock);

    pacer.paceRow(1000);   // anchor
    pacer.paceRow(99999);  // slot 1 -> sleep 250
    pacer.paceRow(50);     // slot 2 -> sleep 250

    EXPECT_EQ(clock.sleeps(), (std::vector<long long>{250, 250}));
}

// The -i grid is absolute too: after an overrun the schedule has already
// banked the missed slots — the late rows emit in an immediate burst and
// only the first row past the banked time waits again.
TEST(CsvReplayPacerTest, ExplicitIntervalCatchesUpAfterOverrun) {
    VirtualClock clock;
    CsvReplayPacer pacer(/*intervalMs=*/100, clock);

    pacer.paceRow(0);      // anchor
    clock.bump(std::chrono::milliseconds(350));  // slots 1..3 already overdue
    pacer.paceRow(0);      // slot 1 (due 100)  -> immediate
    pacer.paceRow(0);      // slot 2 (due 200)  -> immediate
    pacer.paceRow(0);      // slot 3 (due 300)  -> immediate
    pacer.paceRow(0);      // slot 4 (due 400), elapsed 350 -> sleep 50
    pacer.paceRow(0);      // slot 5 (due 500), elapsed 400 -> sleep 100

    EXPECT_EQ(clock.sleeps(), (std::vector<long long>{50, 100}));
}
