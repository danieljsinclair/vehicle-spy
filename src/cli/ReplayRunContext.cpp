#include "vehicle-sim/cli/ReplayRunContext.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"

#include <iostream>
#include <memory>
#include <string>

namespace vehicle_sim::cli {

int ReplayRunContext::run(
    const std::string& filePath,
    const std::string& vehicleType,
    const std::string& logBase,
    domain::DBCTranslationService& translationService) {

    // resolveVehicleContext loads the vehicle's DBC as a side effect
    // (VehicleConfigResolver::resolve calls service.loadVehicle). Essential
    // before processFrame, otherwise it returns nullopt for every frame.
    // The returned context's protocol isn't needed here — replay always feeds
    // the raw TWAI bytes straight to processFrame regardless of vehicle
    // protocol — but we must honor [[nodiscard]].
    (void)resolveVehicleContext(vehicleType, translationService);

    pipeline::FileTransport transport(filePath);
    if (!transport.open()) {
        std::cerr << "Failed to open capture file: " << filePath << "\n";
        return 1;
    }

    // Decoded CSV sink — constructed only when a base was requested. The
    // constructor opens <base>.csv and writes the header; on failure it
    // throws (TraceLogger contract), caught here to fail fast.
    std::unique_ptr<pipeline::DecodedCsvSink> decodedSink;
    if (!logBase.empty()) {
        try {
            decodedSink = std::make_unique<pipeline::DecodedCsvSink>(logBase, translationService.getVehicleId());
        } catch (const std::runtime_error&) {
            std::cerr << "Failed to open CSV log file: " << logBase << ".csv\n";
            return 1;
        }
        if (!decodedSink->isValid()) {
            std::cerr << "Failed to open CSV log file: " << logBase << ".csv\n";
            return 1;
        }
    }

    // For file replay the input file is the raw source of truth, so we do NOT
    // instantiate a RawLogSink (no <base>.raw.txt). Phase 2 transports that
    // lack a pre-existing source file will pass a non-null rawSink here.
    pipeline::CaptureNormaliser normaliser;

    std::cout << "Replaying " << filePath << "\n";

    // Streaming progress: uniform across transports. The reporter lives in the
    // pipeline seam (not the decoder) and throttles itself, so a fast file
    // replay renders a live progress line without flooding the console while a
    // live TCP/BLE stream shows the same view naturally.
    pipeline::ConsoleProgressReporter progress(std::cout);

    auto stats = pipeline::runReplay(transport, normaliser, translationService,
                                     decodedSink.get(), /*rawSink=*/nullptr,
                                     &progress);

    std::cout << "  lines=" << stats.linesRead
              << " frames decoded=" << stats.framesDecoded
              << " skipped=" << stats.skippedLines
              << " malformed=" << stats.malformedLines << "\n";
    std::cout << "Decoded " << stats.framesDecoded << " frames\n";
    return 0;
}

} // namespace vehicle_sim::cli
