#pragma once

#include "vehicle-sim/domain/DBCTranslationService.h"
#include <string>

namespace vehicle_sim::cli {

/**
 * Live telemetry context — runs a LIVE transport (demo/tcp) through the
 * canonical pipeline seam (Transport → Normaliser → DBCTranslationService →
 * RawLogSink + DecodedCsvSink + ConsoleProgressReporter).
 *
 * This is the live counterpart to ReplayRunContext. Both drive the SAME
 * runReplay() loop; the only differences are which transport/normaliser the
 * factory builds and which sinks are wired:
 *   - ReplayRunContext (file): CaptureNormaliser, decoded sink ONLY (the input
 *     file is the raw source of truth — no <base>.raw.txt).
 *   - LiveRunContext (demo/tcp): RawFrameNormaliser, BOTH sinks — the live
 *     stream has no pre-existing raw file, so <base>.raw.txt captures the
 *     verbatim transport lines (source of truth) and <base>.csv the decode.
 *
 * A SIGINT/SIGTERM handler requests a clean stop: the transport's nextLine()
 * observes the stop flag at its next select() timeout and returns nullopt, so
 * runReplay() terminates without hanging.
 */
class LiveRunContext {
public:
    /**
     * Run a live transport through the pipeline.
     *
     * @param connectTarget   "demo" or "tcp:<host>[:<port>]"
     * @param vehicleType     Vehicle type whose DBC decodes the frames
     * @param adapterProtocol Resolved protocol ("raw" or "elm327")
     * @param logBase         Base path for output ("<base>.raw.txt" + "<base>.csv").
     *                        Empty disables both sinks.
     * @param translationService DBC service (vehicle DBC loaded as a side effect)
     * @return 0 on success, 1 on failure (unsupported target, transport open fail)
     */
    static int run(
        const std::string& connectTarget,
        const std::string& vehicleType,
        const std::string& adapterProtocol,
        const std::string& logBase,
        domain::DBCTranslationService& translationService
    );
};

} // namespace vehicle_sim::cli
