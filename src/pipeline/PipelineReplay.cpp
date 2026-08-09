#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/pipeline/PacedFrameScheduler.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/domain/CaptureLog.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/util/IClock.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

namespace vehicle_sim::pipeline {

namespace {

// (The process-wide default clock is a function-local static in
// defaultReplayClock() below — see that definition for why.)

// Count a transport line and mirror it verbatim to the raw sink.
//
// The raw write happens BEFORE normalisation and before any pacing/skip
// decision, so the raw capture stays a faithful replay source: a row skipped
// for decode is still recorded. Shared by both loops so the two cannot drift.
void recordRawLine(ReplayStats& stats, const std::string& line,
                   const ReplayOutputs& outputs) {
    ++stats.linesRead;
    if (outputs.raw) {
        outputs.raw->writeLine(line);
    }
}

// Translate one normalised frame and dispatch the result to the optional sinks.
// The `signal` falsy arm (processFrame returns nullopt) intentionally skips the
// framesDecoded increment — a defensive seam for future translators (the shipped
// DBC pipeline always yields a signal for a well-formed frame), preserved as-is.
void dispatchFrame(
    ReplayStats& stats,
    const domain::DBCTranslationService& translationService,
    const std::vector<std::uint8_t>& bytes,
    std::optional<std::uint64_t> ts,
    const ReplayOutputs& outputs) {

    auto signal = translationService.processFrame(bytes, ts);
    if (!signal) {
        // Diagnostic: surface CAN IDs that the DBC translator does not
        // recognise so operators can see why dbc_signal_count stays at 0
        // without attaching a debugger. Only fires on the live/CLI path;
        // the test-suite replay path is unaffected (no stderr noise).
        const auto lo = static_cast<std::uint16_t>(bytes[0]);
        const auto hi = static_cast<std::uint16_t>(bytes[1]);
        // Widen to uint16 BEFORE shifting: `hi << 8` on a uint8 integer-promotes
        // to int, so the result must be cast back down, which is the narrowing
        // conversion Sonar flags. Doing the arithmetic in uint16 avoids it.
        const auto canId = static_cast<std::uint16_t>(lo | (hi << 8));
        std::cerr << "[decode] CAN 0x" << std::hex << std::setw(3) << std::setfill('0')
                  << canId << std::dec << " — no DBC signal matched ("
                  << bytes.size() << " bytes)\n";
        return;  // held seam: no signal -> do not count as decoded
    }

    ++stats.framesDecoded;

    if (outputs.decoded) {
        outputs.decoded->write(*signal);
    }

    // Progress is reported AFTER the decoded sink is written, so the console
    // never races ahead of the persisted output. The reporter owns throttling.
    if (outputs.progress) {
        outputs.progress->onFrame(*signal, stats.framesDecoded - 1, 0);
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
    const ReplayOutputs& outputs) {
    std::optional<std::uint64_t> ts = hasTimestamp
        ? std::optional<std::uint64_t>(timestampMs)
        : std::nullopt;
    dispatchFrame(stats, translationService, bytes, ts, outputs);
}

// Unpaced loop: dump every frame as fast as it is read (LIVE behaviour).
// Mirrors the original runReplay exactly — no pacing, no blank skipping.
ReplayStats runReplayUnpaced(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs) {

    ReplayStats stats;

    while (auto line = transport.nextLine()) {
        recordRawLine(stats, *line, outputs);

        auto result = normaliser.normalise(*line);
        switch (result.kind) {
            case NormaliserResultKind::Frame: {
                auto bytes = domain::toTwaiFrame(result.frame);
                emitFrame(stats, translationService, bytes, result.hasTimestamp,
                          result.frame.timestampMs, outputs);
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

    if (outputs.progress) {
        outputs.progress->onComplete(stats);
    }

    return stats;
}

// Handle one normalised frame in paced mode: ask the scheduler whether the
// frame is due (it performs any required wait inline), then emit or count it
// as skipped. Extracted so the paced loop reads as a flat switch rather than
// inlining the pacing state machine.
void handlePacedFrame(
    ReplayStats& stats,
    const NormaliserResult& result,
    PacedFrameScheduler& scheduler,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs) {

    const auto& frame = result.frame;

    if (scheduler.consider(frame) == PacedFrameScheduler::Action::Skip) {
        ++stats.skippedLines;
        return;
    }

    auto bytes = domain::toTwaiFrame(frame);
    emitFrame(stats, translationService, bytes, result.hasTimestamp,
              frame.timestampMs, outputs);
}

// Paced loop: REPLAY behaviour. Honour the file's recorded timestamps by
// sleeping until each row's scheduled time arrives relative to replay start;
// skip blank rows (zero timestamp AND zero payload) and rows before
// --start-from. The raw sink (if any) still records verbatim lines BEFORE the
// pacing/skip decision so the capture remains a faithful source.
//
// The pacing state machine itself lives in PacedFrameScheduler (which composes
// the pure ReplayPacing policy); this loop only routes normaliser results.
ReplayStats runReplayPaced(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs,
    util::IClock& clock,
    double startFromS) {

    ReplayStats stats;
    PacedFrameScheduler scheduler(ReplayPacing(startFromS), clock);

    while (auto line = transport.nextLine()) {
        recordRawLine(stats, *line, outputs);

        auto result = normaliser.normalise(*line);
        switch (result.kind) {
            case NormaliserResultKind::Frame:
                handlePacedFrame(stats, result, scheduler, translationService,
                                 outputs);
                break;
            case NormaliserResultKind::Skip:
                ++stats.skippedLines;
                break;
            case NormaliserResultKind::Malformed:
            default:
                ++stats.malformedLines;
                break;
        }
    }

    if (outputs.progress) {
        outputs.progress->onComplete(stats);
    }

    return stats;
}

} // namespace

util::IClock& defaultReplayClock() noexcept {
    // Function-local static, not a global: it is initialised on first use
    // (thread-safe under C++11 magic statics) and is not mutable global state,
    // so it satisfies "global variables should be const" without forcing a
    // const_cast at the call site. SystemClock carries no members — sleepFor()
    // and now() are stateless beyond the steady_clock read — so sharing one
    // instance across concurrent replays is safe.
    static util::SystemClock clock;
    return clock;
}

ReplayStats runReplay(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs,
    ReplayMode mode,
    util::IClock& clock,
    double startFromS) noexcept {

    if (mode == ReplayMode::Paced) {
        return runReplayPaced(transport, normaliser, translationService,
                              outputs, clock, startFromS);
    }
    return runReplayUnpaced(transport, normaliser, translationService, outputs);
}

} // namespace vehicle_sim::pipeline
