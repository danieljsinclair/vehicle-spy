#include "vehicle-sim/cli/ReplayRunContext.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/pipeline/BinaryFileSource.h"
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/CompositeProgressReporter.h"
#include "vehicle-sim/pipeline/CsvStdoutReporter.h"
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
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

    std::ostream& narrative = stdoutCsv ? std::cerr : std::cout;

    // resolveVehicleContext loads the vehicle's DBC as a side effect
    // (VehicleConfigResolver::resolve calls service.loadVehicle). Essential
    // before processFrame, otherwise it returns nullopt for every frame.
    (void)resolveVehicleContext(vehicleType, translationService);

    pipeline::BinaryFileSource source(filePath);
    if (!source.open()) {
        std::cerr << "Failed to open capture file: " << forLog(filePath) << "\n";
        return 1;
    }

    std::unique_ptr<pipeline::DecodedCsvSink> decodedSink;
    if (!logBase.empty()) {
        try {
            decodedSink = std::make_unique<pipeline::DecodedCsvSink>(logBase, translationService.getVehicleId());
        } catch (const domain::TelemetryFileException&) {
            std::cerr << "Failed to open CSV log file: " << forLog(logBase) << ".csv\n";
            return 1;
        }
        if (!decodedSink->isValid()) {
            std::cerr << "Failed to open CSV log file: " << forLog(logBase) << ".csv\n";
            return 1;
        }
    }

    narrative << "Replaying " << forLog(filePath) << "\n";

    pipeline::ConsoleProgressReporter progress(narrative);
    auto stdoutSink = telemetry::createStdoutSink(stdoutCsv, std::cout,
                                                  translationService.getVehicleId());
    pipeline::CsvStdoutReporter csvReporter(*stdoutSink);
    pipeline::CompositeProgressReporter reporters;
    reporters.add(&progress);
    if (stdoutCsv) reporters.add(&csvReporter);

    const pipeline::ReplayOutputs outputs{
        .decoded = decodedSink.get(),
        .raw = nullptr,
        .progress = &reporters,
    };

    auto stats = pipeline::runReplay(source, translationService, outputs,
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
