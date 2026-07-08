#include "vehicle-sim/cli/LiveRunContext.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
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
// handler (one atomic load + one atomic store — no cout/endl). The handler flips
// the token the transports poll; cleared on scope exit via the guard below.
struct LiveStopScope {
    pipeline::StopToken& token;
    explicit LiveStopScope(pipeline::StopToken& t) noexcept : token(t) {
        token.reset();
        pipeline::signal_stop_broker::brokerSet(&token);
        std::signal(SIGINT, vehicle_sim_onStopSignal);
        std::signal(SIGTERM, vehicle_sim_onStopSignal);
    }
    ~LiveStopScope() { pipeline::signal_stop_broker::brokerClear(); }
    // RAII scope guard: non-copyable, non-movable — copying would dangle the
    // StopToken& reference and double-clear the broker on destruction.
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
    domain::DBCTranslationService& translationService) {

    // One cooperative stop signal shared by this run-context and every live
    // transport it builds. The signal handler flips it via the broker; the
    // transports' hot loops poll it and return nullopt promptly on Ctrl+C.
    auto stop = std::make_shared<pipeline::StopToken>();
    LiveStopScope stopScope(*stop);

    auto source = pipeline::buildPipelineSource(connectTarget, adapterProtocol, stop);
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
