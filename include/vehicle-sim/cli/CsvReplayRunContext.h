#pragma once

#include "vehicle-sim/io/CsvTelemetrySource.h"
#include "vehicle-sim/util/IClock.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace vehicle_sim::cli {

/**
 * CSV replay run context — replays a decoded telemetry CSV as if it were
 * live CAN. Deterministic for CI: timestamp-driven (each row due at its
 * recorded offset from the first row), or fixed-rate when --interval is
 * given.
 *
 * Pacing is an absolute schedule owned by CsvReplayPacer (anchored at the
 * first emitted row) and waits via IClock::sleepFor, so tests inject a
 * FakeClock (instant, no real-time sleeps) and stay wall-clock-free while
 * per-row emission overhead shortens the next wait instead of compounding.
 *
 * Usage:
 *   auto source = std::make_unique<io::FileCsvTelemetrySource>(path);
 *   return CsvReplayRunContext::run(std::move(source), vehicleId,
 *                                    intervalMs, std::cout, clock, stdoutCsv);
 */
class CsvReplayRunContext {
public:
    /**
     * Replay a decoded telemetry CSV to `out`.
     *
     * @param source      Owned telemetry source (file-backed).
     * @param vehicleId   Override for the vehicle_id column when non-empty
     *                    (otherwise the row's own vehicle_id is used).
     * @param intervalMs  Fixed inter-row pace in ms (absolute grid from the
     *                    first row). When 0 (default), the source's own
     *                    timestamp_ms offsets drive the pace.
     * @param out         Output stream for the CSV rows.
     * @param clock       Clock used for pacing (FakeClock in tests).
     * @param stdoutCsv   When true, emit only data rows (header + rows, no
     *                    narrative) so stdout stays a clean pipeable CSV.
     * @return 0 on success, 1 on failure (empty source).
     */
    static int run(
        std::unique_ptr<vehicle_sim::io::CsvTelemetrySource> source,
        std::string_view vehicleId,
        int intervalMs,
        std::ostream& out,
        vehicle_sim::util::IClock& clock,
        bool stdoutCsv = false
    );
};

} // namespace vehicle_sim::cli
