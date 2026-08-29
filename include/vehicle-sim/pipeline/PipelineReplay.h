#pragma once

#include "vehicle-sim/util/IClock.h"
#include <cstddef>
#include <memory>

namespace vehicle_sim::domain { class DBCTranslationService; }
namespace vehicle_sim::pipeline {
class IFrameSource;
class DecodedCsvSink;
class RawLogSink;
class IProgressReporter;
}

namespace vehicle_sim::pipeline {

/**
 * Aggregate counters for a single replay run.
 */
struct ReplayStats {
    std::size_t linesRead = 0;
    std::size_t framesDecoded = 0;
    std::size_t malformedLines = 0;
    std::size_t skippedLines = 0;
};

/**
 * Process-wide default clock for replay pacing. See .cpp for why this is a
 * function-local static (thread-safe init, no non-const global).
 */
util::IClock& defaultReplayClock() noexcept;

/**
 * Optional output seams of a replay run. Each is independently nullable.
 */
struct ReplayOutputs {
    DecodedCsvSink* decoded = nullptr;
    RawLogSink* raw = nullptr;
    IProgressReporter* progress = nullptr;
};

/**
 * Pacing mode. Unpaced = live feeds (dump as fast as possible);
 * Paced = replay (sleep to recorded timestamps, skip blank rows).
 */
enum class ReplayMode {
    Unpaced,
    Paced,
};

/**
 * Drive a single IFrameSource through the decoder + sinks pipeline. Uniform
 * across sources: a file replay and a live TCP/USB stream differ ONLY in
 * which IFrameSource is wired in the composition root — the driver does not
 * know. A future WiCan adapter is a new IFrameSource with no driver change.
 *
 * @param source               Opened source (open() must have succeeded).
 * @param translationService   DBC decoder (vehicle DBC must already be loaded).
 * @param outputs              Decoded sink, raw sink, progress reporter — each
 *                             independently nullable.
 * @param mode                 Pacing mode.
 * @param clock                Clock for paced mode. Unused in Unpaced.
 * @param startFromS           Skip rows recorded before this many seconds.
 */
[[nodiscard]] ReplayStats runReplay(
    IFrameSource& source,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs,
    ReplayMode mode = ReplayMode::Unpaced,
    util::IClock& clock = defaultReplayClock(),
    double startFromS = -1.0
) noexcept;

} // namespace vehicle_sim::pipeline
