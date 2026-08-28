#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/IFrameSource.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/pipeline/PacedFrameScheduler.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/util/IClock.h"


#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace vehicle_sim::pipeline {

util::IClock& defaultReplayClock() noexcept {
    // Function-local static (thread-safe init, no global mutable state).
    // SystemClock is stateless beyond a steady_clock read; one instance
    // shared across concurrent replays is safe.
    static util::SystemClock clock;
    return clock;
}

namespace {

void dispatchFrame(
    const domain::DBCTranslationService& translationService,
    const TwaiFrame& f,
    const ReplayOutputs& outputs,
    ReplayStats& stats) {

    std::vector<std::uint8_t> bytes(f.bytes.begin(), f.bytes.end());
    auto signal = translationService.processFrame(bytes, f.timestampMs);
    if (!signal) {
        const auto lo = static_cast<std::uint16_t>(f.bytes[0]);
        const auto hi = static_cast<std::uint16_t>(f.bytes[1]);
        const auto canId = static_cast<std::uint16_t>(lo | (hi << 8));
        std::cerr << "[decode] CAN 0x" << std::hex << std::setw(3) << std::setfill('0')
                  << canId << std::dec << " — no DBC signal matched\n";
        return;
    }
    ++stats.framesDecoded;
    if (outputs.decoded) outputs.decoded->write(*signal);
    if (outputs.progress) outputs.progress->onFrame(*signal, stats.framesDecoded - 1, 0);
}

ReplayStats runReplayUnpaced(
    IFrameSource& source,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs) {

    ReplayStats stats;
    while (auto f = source.nextFrame()) {
        ++stats.linesRead;
        dispatchFrame(translationService, *f, outputs, stats);
    }
    if (outputs.progress) outputs.progress->onComplete(stats);
    return stats;
}

ReplayStats runReplayPaced(
    IFrameSource& source,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs,
    util::IClock& clock,
    double startFromS) {

    ReplayStats stats;
    PacedFrameScheduler scheduler(ReplayPacing(startFromS), clock);
    while (auto f = source.nextFrame()) {
        ++stats.linesRead;
        if (scheduler.consider(*f) == PacedFrameScheduler::Action::Skip) {
            ++stats.skippedLines;
            continue;
        }
        dispatchFrame(translationService, *f, outputs, stats);
    }
    if (outputs.progress) outputs.progress->onComplete(stats);
    return stats;
}

} // namespace

ReplayStats runReplay(
    IFrameSource& source,
    const domain::DBCTranslationService& translationService,
    const ReplayOutputs& outputs,
    ReplayMode mode,
    util::IClock& clock,
    double startFromS) noexcept {

    if (mode == ReplayMode::Paced) {
        return runReplayPaced(source, translationService, outputs, clock, startFromS);
    }
    return runReplayUnpaced(source, translationService, outputs);
}

} // namespace vehicle_sim::pipeline
