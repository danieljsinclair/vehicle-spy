#include "vehicle-sim/cli/ReplayRunContext.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/CompositeProgressReporter.h"
#include "vehicle-sim/pipeline/CsvStdoutReporter.h"
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <iostream>
#include <memory>
#include <string>

namespace vehicle_sim::cli {

int ReplayRunContext::run(
    const std::string& filePath,
    const std::string& vehicleType,
    const std::string& logBase,
    domain::DBCTranslationService& translationService,
    bool stdoutCsv,
    double startFromS) {

    // With --stdout-csv, stdout belongs to the CSV stream alone; every
    // human-readable line moves to stderr so the pipe stays parseable.
    std::ostream& narrative = stdoutCsv ? std::cerr : std::cout;

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
        } catch (const domain::TelemetryFileException&) {
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

    narrative << "Replaying " << filePath << "\n";

    // Streaming progress: uniform across transports. The reporter lives in the
    // pipeline seam (not the decoder) and throttles itself, so a fast file
    // replay renders a live progress line without flooding the console while a
    // live TCP/BLE stream shows the same view naturally.
    pipeline::ConsoleProgressReporter progress(narrative);

    // --stdout-csv adds a SECOND observer on the same seam rather than
    // replacing the console one: the composite fans each decoded frame to both
    // (Open/Closed — runReplay is unchanged).
    auto stdoutSink = telemetry::createStdoutSink(stdoutCsv, std::cout,
                                                  translationService.getVehicleId());
    pipeline::CsvStdoutReporter csvReporter(*stdoutSink);
    pipeline::CompositeProgressReporter reporters;
    reporters.add(&progress);
    if (stdoutCsv) {
        reporters.add(&csvReporter);
    }

    // REPLAY mode: pace output to the file's recorded timestamps and skip
    // blank rows + --start-from-prior rows. The live path (LiveRunContext)
    // stays Unpaced so it reflects reality (dump as fast as possible).
    // For file replay the input file is already the raw source of truth, so no
    // raw sink is wired here (see the decodedSink comment above).
    const pipeline::ReplayOutputs outputs{
        .decoded = decodedSink.get(),
        .raw = nullptr,
        .progress = &reporters,
    };

    auto stats = pipeline::runReplay(transport, normaliser, translationService,
                                     outputs,
                                     pipeline::ReplayMode::Paced,
                                     pipeline::defaultReplayClock(),
                                     startFromS);

    narrative << "  lines=" << stats.linesRead
              << " frames decoded=" << stats.framesDecoded
              << " skipped=" << stats.skippedLines
              << " malformed=" << stats.malformedLines << "\n";
    narrative << "Decoded " << stats.framesDecoded << " frames\n";
    return 0;
}

} // namespace vehicle_sim::cli
