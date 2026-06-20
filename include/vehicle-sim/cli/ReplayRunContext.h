#pragma once

#include "vehicle-sim/domain/DBCTranslationService.h"
#include <string>

namespace vehicle_sim::cli {

/**
 * Offline replay context — decouples development from live hardware.
 *
 * Phase 1 routes file replay through the new Transport → Normaliser →
 * Decoder → Sink pipeline (FileTransport → RawFrameNormaliser →
 * DBCTranslationService → DecodedCsvSink). The input file IS the raw source
 * of truth, so replay writes ONLY <base>.csv — never <base>.raw.txt.
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
     * @return 0 on success, 1 on failure (file missing, logger invalid, unknown vehicle)
     */
    static int run(
        const std::string& filePath,
        const std::string& vehicleType,
        const std::string& logBase,
        domain::DBCTranslationService& translationService
    );
};

} // namespace vehicle_sim::cli
