#include "vehicle-sim/cli/CsvReplayPacer.h"

#include <chrono>

namespace vehicle_sim::cli {

CsvReplayPacer::CsvReplayPacer(int intervalMs, util::IClock& clock) noexcept
    : intervalMs_(intervalMs), clock_(clock) {}

void CsvReplayPacer::paceRow(std::uint64_t rowTsMs) {
    if (!anchored_) {
        // The first row IS the schedule's t=0: it anchors both origins and
        // emits immediately (never a wait before replay starts).
        baselineTsMs_ = rowTsMs;
        replayStart_ = clock_.now();
        anchored_ = true;
        rowsPaced_ = 1;
        return;
    }
    if (const std::int64_t waitMs =
            scheduledOffsetMs(rowTsMs) - static_cast<std::int64_t>(elapsedMs());
        waitMs > 0) {
        // Absolute-deadline wait: remaining = deadline - now, where the
        // deadline is replayStart_ + scheduledOffset. Every prior row's
        // read/encode/flush overhead is already inside elapsedMs(), so it
        // shortens THIS wait instead of trailing behind the schedule; an
        // overdue row (waitMs <= 0) skips the wait entirely and emits now.
        clock_.sleepFor(std::chrono::milliseconds(waitMs));
    }
    ++rowsPaced_;
}

std::int64_t CsvReplayPacer::scheduledOffsetMs(std::uint64_t rowTsMs) const {
    if (intervalMs_ > 0) {
        // Explicit -i grid: this row occupies the rowsPaced_-th slot after
        // the origin (the anchor occupied slot 0).
        return static_cast<std::int64_t>(rowsPaced_) * intervalMs_;
    }
    // Timestamp-driven: due when wall time since the origin equals the
    // row's recorded distance from the first row. Cast BEFORE subtracting
    // so an out-of-order row yields a small negative (overdue), never a
    // uint64 wrap-around (same arithmetic as ReplayPacing::classifyFrame).
    return static_cast<std::int64_t>(rowTsMs) -
           static_cast<std::int64_t>(baselineTsMs_);
}

std::uint64_t CsvReplayPacer::elapsedMs() const {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_.now() - replayStart_).count();
    return static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed);
}

}  // namespace vehicle_sim::cli
