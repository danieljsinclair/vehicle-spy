#include "vehicle-sim/cli/LiveRunContext.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/USBTransport.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>

namespace {

// Process-wide stop flag for the live loop. Set by the SIGINT/SIGTERM handler
// and polled by live transports with select() timeouts. For the demo transport
// the loop is bounded so no flag is needed, but the handler is installed
// uniformly so Ctrl+C works for every live source.
std::atomic g_liveRunning(true);

void liveSignalHandler(int sigNum) {
    std::cout << "\nReceived signal " << sigNum << ", shutting down..." << std::endl;
    g_liveRunning = false;
    // Ask live transports to return nullopt at their next select() timeout.
    vehicle_sim::pipeline::TCPTransport::requestStop();
    vehicle_sim::pipeline::USBTransport::requestStop();
}

void installLiveSignalHandlers() {
    // #10 (signal-handler conflict): TelemetryRunner and BLERunContext each
    // install their own SIGINT handler. LiveRunContext installs its own here
    // so Ctrl+C cleanly stops the live pipeline. This is a one-shot install at
    // run() entry (per-process). The BLE migration (#18) will unify these into
    // a single handler; until then each run context owns its own, which is safe
    // because only one runs per process.
    std::signal(SIGINT, liveSignalHandler);
    std::signal(SIGTERM, liveSignalHandler);
    g_liveRunning = true;
    vehicle_sim::pipeline::TCPTransport::resetStop();
    vehicle_sim::pipeline::USBTransport::resetStop();
}

} // namespace

namespace vehicle_sim::cli {

int LiveRunContext::run(
    const std::string& connectTarget,
    const std::string& vehicleType,
    const std::string& adapterProtocol,
    const std::string& logBase,
    domain::DBCTranslationService& translationService) {

    auto source = pipeline::buildPipelineSource(connectTarget, adapterProtocol);
    if (!source.transport || !source.normaliser) {
        std::cerr << "Unsupported live connect target: " << connectTarget << "\n";
        return 1;
    }

    // resolveVehicleContext loads the vehicle's DBC as a side effect (must
    // happen before processFrame, otherwise it returns nullopt for every frame).
    (void)resolveVehicleContext(vehicleType, translationService);

    if (!source.transport->open()) {
        std::cerr << "Failed to open live transport: " << connectTarget << "\n";
        return 1;
    }

    // LIVE wiring: BOTH sinks. The raw sink captures the verbatim transport
    // stream (the source of truth — a live source has no pre-existing raw
    // file), and the decoded sink writes the derived CSV. Constructed only when
    // a base was requested.
    std::unique_ptr<pipeline::RawLogSink> rawSink;
    std::unique_ptr<pipeline::DecodedCsvSink> decodedSink;
    if (!logBase.empty()) {
        rawSink = std::make_unique<pipeline::RawLogSink>(logBase);
        if (!rawSink->isValid()) {
            std::cerr << "Failed to open raw log file: " << logBase << ".raw.txt\n";
            return 1;
        }
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

    installLiveSignalHandlers();

    std::cout << "Streaming " << connectTarget << " (" << vehicleType << ")\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    // The SAME runReplay loop serves file replay and live — uniform across
    // transports (Open/Closed). For live TCP the loop ends when the stop flag
    // makes nextLine() return nullopt; for bounded demo it ends at EOF.
    pipeline::ConsoleProgressReporter progress(std::cout, translationService.getVehicleId());
    auto stats = pipeline::runReplay(*source.transport, *source.normaliser,
                                     translationService, decodedSink.get(),
                                     rawSink.get(), &progress);

    std::cout << "\n  lines=" << stats.linesRead
              << " frames decoded=" << stats.framesDecoded
              << " skipped=" << stats.skippedLines
              << " malformed=" << stats.malformedLines << "\n";
    std::cout << "Goodbye!\n";
    return 0;
}

} // namespace vehicle_sim::cli
