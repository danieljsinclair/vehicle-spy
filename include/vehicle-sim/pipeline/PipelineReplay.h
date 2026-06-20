#pragma once

#include "vehicle-sim/pipeline/IAdapterNormaliser.h"
#include "vehicle-sim/pipeline/ITransport.h"
#include <cstddef>
#include <memory>

namespace vehicle_sim::domain { class DBCTranslationService; }
namespace vehicle_sim::pipeline { class DecodedCsvSink; class RawLogSink; class IProgressReporter; }
namespace vehicle_sim::telemetry { class CsvStdoutSink; }

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
 * @param stdoutCsvSink      Optional CSV sink for stdout pipe output, or nullptr.
 *                           When supplied, each decoded frame is written as a
 *                           CSV row to stdout (for piping to engine-sim-cli).
 * @return                   Aggregate stats for the run.
 */
[[nodiscard]] ReplayStats runReplay(
    ITransport& transport,
    IAdapterNormaliser& normaliser,
    domain::DBCTranslationService& translationService,
    DecodedCsvSink* decodedSink,
    RawLogSink* rawSink,
    IProgressReporter* progressReporter = nullptr,
    telemetry::CsvStdoutSink* stdoutCsvSink = nullptr
) noexcept;

} // namespace vehicle_sim::pipeline
