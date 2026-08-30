#include "vehicle-sim/cli/LiveRunContext.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/CompositeProgressReporter.h"
#include "vehicle-sim/pipeline/CsvStdoutReporter.h"
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/Elm327Normaliser.h"
#include "vehicle-sim/pipeline/LiveTwaiSource.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/pipeline/SignalStopBroker.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <csignal>
#include <iostream>
#include <memory>

namespace vehicle_sim::cli {

namespace {

// Publish the live StopToken to the broker and install the async-signal-safe
// handler. Cleared on scope exit via the RAII guard.
struct LiveStopScope {
    pipeline::StopToken& token;
    explicit LiveStopScope(pipeline::StopToken& t) noexcept : token(t) {
        token.reset();
        pipeline::signal_stop_broker::brokerSet(&token);
        std::signal(SIGINT, vehicle_sim_onStopSignal);
        std::signal(SIGTERM, vehicle_sim_onStopSignal);
    }
    ~LiveStopScope() { pipeline::signal_stop_broker::brokerClear(); }
    LiveStopScope(const LiveStopScope&) = delete;
    LiveStopScope& operator=(const LiveStopScope&) = delete;
    LiveStopScope(LiveStopScope&&) = delete;
    LiveStopScope& operator=(LiveStopScope&&) = delete;
};

} // namespace

int LiveRunContext::run(
    const std::string& connectTarget,
    const std::string& vehicleType,
    const std::string& adapterProtocol,
    const std::string& logBase,
    domain::DBCTranslationService& translationService,
    bool stdoutCsv) {

    std::ostream& narrative = stdoutCsv ? std::cerr : std::cout;

    auto stop = std::make_shared<pipeline::StopToken>();
    LiveStopScope stopScope(*stop);

    auto source = pipeline::buildPipelineSource(connectTarget, adapterProtocol, stop);
    if (!source.transport) {
        std::cerr << "Unsupported live connect target: " << forLog(connectTarget) << "\n";
        return 1;
    }

    (void)resolveVehicleContext(vehicleType, translationService);

    if (!source.transport->open()) {
        std::cerr << "Failed to open live transport: " << forLog(connectTarget) << "\n";
        return 1;
    }

    // ELM327 mode routes each line through the ELM327 normaliser; raw mode
    // uses the inline tokeniser in LiveTwaiSource.
    pipeline::Elm327Normaliser elmNormaliser;
    pipeline::IAdapterNormaliser* normaliser =
        (adapterProtocol == "elm327") ? &elmNormaliser : nullptr;
    pipeline::LiveTwaiSource twaiSource(*source.transport, normaliser);

    std::unique_ptr<pipeline::RawLogSink> rawSink;
    std::unique_ptr<pipeline::DecodedCsvSink> decodedSink;
    if (!logBase.empty()) {
        rawSink = std::make_unique<pipeline::RawLogSink>(logBase);
        if (!rawSink->isValid()) {
            std::cerr << "Failed to open raw log file: " << forLog(logBase) << ".raw.txt\n";
            return 1;
        }
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

    narrative << "Streaming " << forLog(connectTarget) << " (" << forLog(vehicleType) << ")\n";
    narrative << "Press Ctrl+C to stop\n\n";

    pipeline::ConsoleProgressReporter progress(narrative, translationService.getVehicleId());
    auto stdoutSink = telemetry::createStdoutSink(stdoutCsv, std::cout,
                                                  translationService.getVehicleId());
    pipeline::CsvStdoutReporter csvReporter(*stdoutSink);
    pipeline::CompositeProgressReporter reporters;
    reporters.add(&progress);
    if (stdoutCsv) reporters.add(&csvReporter);

    const pipeline::ReplayOutputs outputs{
        .decoded = decodedSink.get(),
        .raw = rawSink.get(),
        .progress = &reporters,
    };

    auto stats = pipeline::runReplay(twaiSource, translationService, outputs);

    narrative << "\n  lines=" << stats.linesRead
              << " frames decoded=" << stats.framesDecoded
              << " skipped=" << stats.skippedLines
              << " malformed=" << stats.malformedLines << "\n";
    narrative << "Goodbye!\n";
    return 0;
}

} // namespace vehicle_sim::cli
