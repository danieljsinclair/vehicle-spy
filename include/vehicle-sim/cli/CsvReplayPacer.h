#pragma once

#include "vehicle-sim/util/IClock.h"

#include <cstdint>

namespace vehicle_sim::cli {

/**
 * Absolute-schedule pacer for decoded-CSV replay (CsvReplayRunContext).
 *
 * Same pacing idiom as the raw-CAN seam's PacedFrameScheduler — a
 * deliberate mirror of that master pattern, NOT a second pattern; do not
 * regress either path back to relative per-row sleeps:
 *
 *   - ONE fixed pacing origin, anchored when the first row is emitted
 *     (replayStart = clock.now(), baselineTsMs = that row's timestamp).
 *     The first row never waits, so replay starts immediately.
 *   - Each later row's wait is RECOMPUTED against that origin:
 *
 *       wait = scheduledOffset(row) - elapsed(now - replayStart)
 *
 *     so time spent reading/encoding/flushing between rows SHORTENS the
 *     next wait instead of trailing behind it. The relative
 *     sleepFor(rowDelta) loop this replaces ran at 0.793x real time
 *     (351.8 vs 441.8 rows/wall-s) because every sleep's overshoot was
 *     additive and the error compounded per row.
 *   - A row that is already overdue (wait <= 0) emits immediately: the
 *     schedule catches up in a burst instead of drifting. An out-of-order
 *     row (earlier timestamp than a predecessor) is simply overdue and
 *     can never make replay sleep backwards.
 *
 * Two schedules share that arithmetic:
 *   - intervalMs <= 0 (default replay): timestamp-driven — a row is due
 *     when wall time since the origin equals its recorded timestamp
 *     distance from the first row.
 *   - intervalMs > 0 (explicit -i/--interval override): fixed grid — the
 *     k-th row after the origin is due k*intervalMs after it, regardless
 *     of the recorded timestamps.
 *
 * The wait itself is IClock::sleepFor(remaining) — the same primitive
 * PacedFrameScheduler uses. On a SystemClock that parks until the absolute
 * deadline (remaining = deadline - now); on a FakeClock it advances
 * virtual time, keeping tests wall-clock-free.
 *
 * Not thread-safe, not reusable across runs: one instance per replay run
 * (mirrors PacedFrameScheduler's lifetime contract).
 */
class CsvReplayPacer {
public:
    CsvReplayPacer(int intervalMs, util::IClock& clock) noexcept;

    CsvReplayPacer(const CsvReplayPacer&) = delete;
    CsvReplayPacer& operator=(const CsvReplayPacer&) = delete;

    /**
     * Pace the next row against the absolute schedule, waiting inline if
     * the row is scheduled in the future. The FIRST paced row anchors the
     * origin and returns without waiting.
     *
     * @param rowTsMs the row's recorded timestamp_ms (consulted only by
     *        the timestamp-driven schedule; the -i grid ignores it).
     */
    void paceRow(std::uint64_t rowTsMs);

private:
    [[nodiscard]] std::uint64_t elapsedMs() const;
    [[nodiscard]] std::int64_t scheduledOffsetMs(std::uint64_t rowTsMs) const;

    int intervalMs_;
    util::IClock& clock_;
    util::IClock::time_point replayStart_{};
    std::uint64_t baselineTsMs_ = 0;
    std::uint64_t rowsPaced_ = 0;
    bool anchored_ = false;
};

}  // namespace vehicle_sim::cli
