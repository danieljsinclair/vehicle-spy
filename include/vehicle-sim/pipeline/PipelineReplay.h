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
 * shared by all replay runs (pacing only reads now() and sleeps via sleepFor,
 * both thread-safe and stateless beyond the steady_clock read). Lives in the
 * pipeline so the no-clock overload of runReplay can default to real wall-clock
 * pacing without every caller injecting a clock. Declared before runReplay so it
 * is visible as a default-argument value.
 *
 * Returns a NON-const reference because pacing must call sleepFor(), which is a
 * mutating operation on the IClock interface (a FakeClock advances its virtual
 * time). The instance is a function-local static rather than a mutable global,
 * so there is no non-const global state.
 */
util::IClock& defaultReplayClock() noexcept;

/**
 * The optional output seams of a replay run, grouped because they are one
 * cohesive concern: "where does this run's output go?". Each is independently
 * nullable — a run may persist decoded rows, mirror verbatim lines, report
 * progress, any combination, or none.
 *
 * Deliberately NOT a god-struct: the DI seams (transport, normaliser,
 * translator) and the run configuration (mode, clock, start-from) stay as
 * explicit parameters. Bundling those in too would hide the very SRP violation
 * this refactor removes, rather than expressing a real grouping.
 */
struct ReplayOutputs {
    /** Decoded CSV sink, or nullptr to skip decoded output. */
    DecodedCsvSink* decoded = nullptr;
    /** Raw verbatim sink, or nullptr to skip raw output. */
    RawLogSink* raw = nullptr;
    /**
     * Streaming progress observer, or nullptr to run silently. When supplied,
     * onFrame() fires after each decoded frame and onComplete() once the
     * transport is exhausted. Uniform across transports: it consumes the
     * decoded VehicleSignal, not transport bytes.
     */
    IProgressReporter* progress = nullptr;
};

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
 * @param outputs            Where this run's output goes (decoded sink, raw
 *                           sink, progress reporter) — each independently
 *                           nullable. Brace-init at the call site, e.g.
 *                           `{&decoded, nullptr, &reporter}`.
 * @param mode               Pacing mode. Unpaced for live feeds (dump as fast
 *                           as possible); Paced for replay (sleep to recorded
 *                           timestamps, skip blanks + --start-from-prior rows).
 * @param clock              Clock used for Paced-mode elapsed-time reads and
 *                           sleeps. Unused in Unpaced mode. Defaults to a real
 *                           SystemClock when omitted, so existing callers need
 *                           not inject one.
 * @param startFromS         Skip rows whose recorded timestamp is before this
 *                           many seconds (REPLAY --start-from). Negative means
 *                           "not set" (skip nothing). Ignored in Unpaced mode.
 * @return                   Aggregate stats for the run.
 */
[[nodiscard]] ReplayStats runReplay(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs,
    ReplayMode mode = ReplayMode::Unpaced,
    util::IClock& clock = defaultReplayClock(),
    double startFromS = -1.0
) noexcept;

} // namespace vehicle_sim::pipeline
