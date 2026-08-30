#pragma once

#include "vehicle-sim/domain/DBCTranslationService.h"
#include <string>

namespace vehicle_sim::cli {

/**
 * Offline replay context — decouples development from live hardware.
 *
 * Routes file replay through the IFrameSource-driven pipeline
 * (BinaryFileSource → DBCTranslationService → DecodedCsvSink). The input
 * file IS the raw source of truth, so replay writes ONLY <base>.csv — never
 * <base>.raw.txt.
 *
 * Mirrors BLERunContext's static-run style but reads from a file instead of
 * a BLE adapter. Deliberately a separate context (SRP): it shares nothing
 * mutable with the live path and needs no SignalSource.
 */
class ReplayRunContext {
public:
    /**
     * Replay a capture file through the DBC translation pipeline.
     *
     * @param filePath    Path to a raw CAN capture CSV (legacy or verbatim form)
     * @param vehicleType Vehicle type whose DBC should decode the frames
     * @param logBase     Base path for decoded output ("<base>.csv"). Empty to skip.
     * @param translationService DBC translation service (vehicle DBC loaded as side effect)
     * @param stdoutCsv   When true, stream decoded CSV rows to stdout and move
     *                    the human-readable progress/summary to stderr, so the
     *                    stdout stream stays a clean pipeable CSV.
     * @param startFromS  Replay-only: skip rows whose recorded timestamp is
     *                    before this many seconds (measured from the
     *                    recording's first frame). Negative means "no skip".
     *                    When skipping with stdoutCsv, a "#vs-start-from <s>"
     *                    comment line is emitted before the CSV header so
     *                    downstream consumers can keep their timecode
     *                    relative to the recording's true start.
     *                    Paces the replay to the file's recorded timestamps.
     * @return 0 on success, 1 on failure (file missing, logger invalid, unknown vehicle)
     */
    static int run(
        const std::string& filePath,
        const std::string& vehicleType,
        const std::string& logBase,
        domain::DBCTranslationService& translationService,
        bool stdoutCsv = false,
        double startFromS = -1.0
    );
};

} // namespace vehicle_sim::cli
