#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"
#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/util/IClock.h"
#include <cstddef>
#include <memory>

namespace vehicle_sim::domain { class DBCTranslationService; }
namespace vehicle_sim::pipeline { class DecodedCsvSink; class RawLogSink; class IProgressReporter; }

namespace vehicle_sim::pipeline {

/**
 * Drives the Transport → Normaliser → Decoder → Sinks pipeline for a bounded
 * (file-style) replay. This is the Phase 1 wiring: FileTransport →
 * RawFrameNormaliser → DBCTranslationService → DecodedCsvSink. It owns no
 * protocol knowledge — every policy object is injected (Open/Closed). Later
 * phases reuse this driver with different transport/normaliser impls.
 *
 * decodedSink may be null (decode-disabled replay); rawSink may be null
 * (raw-disabled replay). For Phase 1 file replay, rawSink is deliberately
 * null because the input file is already the source of truth.
 */
struct ReplayStats {
    std::size_t linesRead = 0;
    std::size_t framesDecoded = 0;
    std::size_t malformedLines = 0;
    std::size_t skippedLines = 0;
};

/**
 * Process-wide default clock for replay pacing. A single SystemClock instance
 * shared by all replay runs (pacing only reads now()/sleeps via sleepFor, which
 * are thread-safe and stateless apart from the steady_clock read). Lives in the
 * pipeline so the no-clock overload of runReplay can default to real wall-clock
 * pacing without every caller injecting a clock. Declared before runReplay so it
 * is visible as a default-argument value.
 */
const util::IClock& defaultReplayClock() noexcept;

/**
 * Pacing mode for a replay run.
 *   - Unpaced: emit each frame as soon as it is read (LIVE feeds reflect
 *     reality — dump as fast as possible, no timestamp pacing, no blank skip).
 *   - Paced:   honour the file's recorded timestamps — sleep until each row's
 *     scheduled time arrives relative to replay start (REPLAY mode). In Paced
 *     mode blank rows (zero timestamp AND zero payload) are skipped, and rows
 *     before --start-from are skipped.
 */
enum class ReplayMode {
    Unpaced,
    Paced,
};

/**
 * Run a bounded replay through the pipeline.
 *
 * @param transport          Opened transport (open() must already have succeeded).
 * @param normaliser         Adapter-protocol normaliser (e.g. RawFrameNormaliser).
 * @param translationService DBC decoder (vehicle DBC must already be loaded).
 * @param decodedSink        Decoded CSV sink, or nullptr to skip decoded output.
 * @param rawSink            Raw verbatim sink, or nullptr to skip raw output.
 * @param progressReporter   Optional streaming progress observer, or nullptr
 *                           to run silently (the Phase 1 default for any path
 *                           that does not want live console output). When
 *                           supplied, onFrame() is called after each decoded
 *                           frame and onComplete() once the transport is
 *                           exhausted. Uniform across transports — the same
 *                           reporter serves file/tcp/ble because it consumes
 *                           the decoded VehicleSignal, not transport bytes.
 * @param mode               Pacing mode. Unpaced for live feeds (dump as fast
 *                           as possible); Paced for replay (sleep to recorded
 *                           timestamps, skip blanks + --start-from-prior rows).
 * @param clock              Clock used for Paced-mode sleeps. Unused in Unpaced
 *                           mode. Defaults to a real SystemClock when omitted,
 *                           so existing callers need not inject one.
 * @param startFromS         Skip rows whose recorded timestamp is before this
 *                           many seconds (REPLAY --start-from). Negative means
 *                           "not set" (skip nothing). Ignored in Unpaced mode.
 * @return                   Aggregate stats for the run.
 */
[[nodiscard]] ReplayStats runReplay(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    DecodedCsvSink* decodedSink,
    RawLogSink* rawSink,
    IProgressReporter* progressReporter = nullptr,
    ReplayMode mode = ReplayMode::Unpaced,
    const util::IClock& clock = defaultReplayClock(),
    double startFromS = -1.0
) noexcept;

} // namespace vehicle_sim::pipeline
