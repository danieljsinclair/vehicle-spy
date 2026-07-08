#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/domain/CaptureLog.h"
#include "vehicle-sim/domain/DBCTranslationService.h"

#include <optional>

namespace vehicle_sim::pipeline {

namespace {

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
        return;  // held seam: no signal -> do not count as decoded
    }

    ++stats.framesDecoded;

    if (decodedSink) {
        decodedSink->write(*signal);
    }

    // Progress is reported AFTER the decoded sink is written, so the console
    // never races ahead of the persisted output. The reporter owns throttling;
    // passing 0 for totalHints keeps runReplay transport-agnostic (a total is
    // not knowable for live streams — callers that know it can supply it later
    // via the same seam).
    if (progressReporter) {
        progressReporter->onFrame(*signal, stats.framesDecoded - 1, 0);
    }
}

} // namespace

ReplayStats runReplay(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    const domain::DBCTranslationService& translationService,
    DecodedCsvSink* decodedSink,
    RawLogSink* rawSink,
    IProgressReporter* progressReporter) noexcept {

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
                std::optional<std::uint64_t> ts = result.hasTimestamp
                    ? std::optional<std::uint64_t>(result.frame.timestampMs)
                    : std::nullopt;
                dispatchFrame(stats, translationService, bytes, ts,
                              decodedSink, progressReporter);
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

} // namespace vehicle_sim::pipeline
