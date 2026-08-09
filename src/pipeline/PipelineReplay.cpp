#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/domain/CaptureLog.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/util/IClock.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>

namespace vehicle_sim::pipeline {

namespace {

// Process-wide default replay clock (see defaultReplayClock() declaration in
// the header). One real SystemClock; pacing only reads now()/sleeps, both
// thread-safe and stateless beyond the steady_clock read.
static const util::SystemClock g_defaultReplayClock;

// Translate one normalised frame and dispatch the result to the optional sinks.
// The `signal` falsy arm (processFrame returns nullopt) intentionally skips the
// framesDecoded increment — a defensive seam for future translators (the shipped
// DBC pipeline always yields a signal for a well-formed frame), preserved as-is.
void dispatchFrame(
    ReplayStats& stats,
    const domain::DBCTranslationService& translationService,
    const std::vector<std::uint8_t>& bytes,
    std::optional<std::uint64_t> ts,
    DecodedCsvSink* decodedSink,
    IProgressReporter* progressReporter) {

    auto signal = translationService.processFrame(bytes, ts);
    if (!signal) {
        // Diagnostic: surface CAN IDs that the DBC translator does not
        // recognise so operators can see why dbc_signal_count stays at 0
        // without attaching a debugger. Only fires on the live/CLI path;
        // the test-suite replay path is unaffected (no stderr noise).
        const std::uint8_t lo = static_cast<std::uint8_t>(bytes[0]);
        const std::uint8_t hi = static_cast<std::uint8_t>(bytes[1]);
        const std::uint16_t canId =
            static_cast<std::uint16_t>(lo) |
            (static_cast<std::uint16_t>(hi) << 8);
        std::cerr << "[decode] CAN 0x" << std::hex << std::setw(3) << std::setfill('0')
                  << canId << std::dec << " — no DBC signal matched ("
                  << bytes.size() << " bytes)\n";
        return;  // held seam: no signal -> do not count as decoded
    }

    ++stats.framesDecoded;

    if (decodedSink) {
        decodedSink->write(*signal);
    }

    // Progress is reported AFTER the decoded sink is written, so the console
    // never races ahead of the persisted output. The reporter owns throttling.
    if (progressReporter) {
        progressReporter->onFrame(*signal, stats.framesDecoded - 1, 0);
    }
}

// Emit a single normalised frame through the decoder + sinks. Shared by both
// the paced and unpaced loops so the decode/sink contract is identical.
void emitFrame(
    ReplayStats& stats,
    const domain::DBCTranslationService& translationService,
    const std::vector<std::uint8_t>& bytes,
    bool hasTimestamp,
    std::uint64_t timestampMs,
    DecodedCsvSink* decodedSink,
    IProgressReporter* progressReporter) {
    std::optional<std::uint64_t> ts = hasTimestamp
        ? std::optional<std::uint64_t>(timestampMs)
        : std::nullopt;
    dispatchFrame(stats, translationService, bytes, ts,
                  decodedSink, progressReporter);
}

// Unpaced loop: dump every frame as fast as it is read (LIVE behaviour).
// Mirrors the original runReplay exactly — no pacing, no blank skipping.
ReplayStats runReplayUnpaced(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    DecodedCsvSink* decodedSink,
    RawLogSink* rawSink,
    IProgressReporter* progressReporter) {

    ReplayStats stats;

    while (auto line = transport.nextLine()) {
        ++stats.linesRead;

        // The raw sink records the verbatim transport line BEFORE normalisation
        // so the capture is a faithful replay source.
        if (rawSink) {
            rawSink->writeLine(*line);
        }

        auto result = normaliser.normalise(*line);
        switch (result.kind) {
            case NormaliserResultKind::Frame: {
                auto bytes = domain::toTwaiFrame(result.frame);
                emitFrame(stats, translationService, bytes, result.hasTimestamp,
                          result.frame.timestampMs, decodedSink, progressReporter);
                break;
            }
            case NormaliserResultKind::Skip:
                ++stats.skippedLines;
                break;
            case NormaliserResultKind::Malformed:
            default:
                ++stats.malformedLines;
                break;
        }
    }

    if (progressReporter) {
        progressReporter->onComplete(stats);
    }

    return stats;
}

// Paced loop: REPLAY behaviour. Honour the file's recorded timestamps by
// sleeping until each row's scheduled time arrives relative to replay start;
// skip blank rows (zero timestamp AND zero payload) and rows before
// --start-from. The raw sink (if any) still records verbatim lines BEFORE the
// pacing/skip decision so the capture remains a faithful source.
//
// Pacing design (per directive): capture the first emitted frame's recorded
// timestamp as the baseline; for each subsequent frame, sleep until
// (now - replayStart) >= (frame.timestampMs - baseline). Uses a steady_clock
// via the injected IClock. Simple and correct; Live mode stays unpaced.
ReplayStats runReplayPaced(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    DecodedCsvSink* decodedSink,
    RawLogSink* rawSink,
    IProgressReporter* progressReporter,
    const util::IClock& clock,
    double startFromS) {

    ReplayStats stats;
    ReplayPacing pacing(startFromS);

    const auto replayStart = clock.now();
    bool baselineSet = false;
    std::uint64_t baselineTsMs = 0;

    while (auto line = transport.nextLine()) {
        ++stats.linesRead;

        // Verbatim capture is recorded BEFORE pacing/skip so the raw source of
        // truth is preserved regardless of replay decisions.
        if (rawSink) {
            rawSink->writeLine(*line);
        }

        auto result = normaliser.normalise(*line);
        switch (result.kind) {
            case NormaliserResultKind::Frame: {
                const auto& frame = result.frame;

                // Anchor the baseline on the first frame carrying telemetry
                // (non-blank). Blank rows do not advance the baseline.
                if (!baselineSet && !ReplayPacing::isFrameBlank(frame)) {
                    baselineTsMs = frame.timestampMs;
                    baselineSet = true;
                }

                // Skip blank rows (REPLAY-only: the live path records everything).
                if (ReplayPacing::isFrameBlank(frame)) {
                    ++stats.skippedLines;
                    break;
                }

                // Pace: sleep until this row's scheduled time arrives. Before the
                // baseline is known (first real frame) we surface immediately.
                if (baselineSet) {
                    const auto elapsed =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            clock.now() - replayStart).count();
                    const std::int64_t waitMs =
                        pacing.classifyFrame(frame, baselineTsMs,
                                            static_cast<std::uint64_t>(
                                                elapsed < 0 ? 0 : elapsed));
                    if (waitMs > 0) {
                        const_cast<util::IClock&>(clock).sleepFor(
                            std::chrono::milliseconds(waitMs));
                    }
                    // waitMs < 0 => before --start-from: skip (counted as skipped).
                    if (waitMs < 0) {
                        ++stats.skippedLines;
                        break;
                    }
                }

                auto bytes = domain::toTwaiFrame(frame);
                emitFrame(stats, translationService, bytes, result.hasTimestamp,
                          frame.timestampMs, decodedSink, progressReporter);
                break;
            }
            case NormaliserResultKind::Skip:
                ++stats.skippedLines;
                break;
            case NormaliserResultKind::Malformed:
            default:
                ++stats.malformedLines;
                break;
        }
    }

    if (progressReporter) {
        progressReporter->onComplete(stats);
    }

    return stats;
}

} // namespace

const util::IClock& defaultReplayClock() noexcept {
    return g_defaultReplayClock;
}

ReplayStats runReplay(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    DecodedCsvSink* decodedSink,
    RawLogSink* rawSink,
    IProgressReporter* progressReporter,
    ReplayMode mode,
    const util::IClock& clock,
    double startFromS) noexcept {

    if (mode == ReplayMode::Paced) {
        return runReplayPaced(transport, normaliser, translationService,
                              decodedSink, rawSink, progressReporter,
                              clock, startFromS);
    }
    return runReplayUnpaced(transport, normaliser, translationService,
                            decodedSink, rawSink, progressReporter);
}

} // namespace vehicle_sim::pipeline
