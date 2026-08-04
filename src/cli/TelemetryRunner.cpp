#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/presentation/VehicleSignalFormatter.h"
#include "vehicle-sim/domain/EventDispatcher.h"
#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/RawTraceLogger.h"
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace {
    constexpr int SPIN_SLEEP_MS = 10;

    struct TelemetryPipeline {
        vehicle_sim::domain::EventDispatcher dispatcher;
        std::unique_ptr<vehicle_sim::telemetry::TraceLogger> csvLogger;
        std::unique_ptr<vehicle_sim::telemetry::RawTraceLogger> rawLogger;
        std::unique_ptr<vehicle_sim::telemetry::ICsvStdoutSink> stdoutSink;
        int dispatchCount_ = 0;

        bool setup(const std::string& logCsvPath,
                   const std::string& logRawPath,
                   std::ostream& outStream,
                   bool stdoutCsv,
                   std::ostream* stdoutCsvStream) {
            using namespace vehicle_sim;

            if (!logCsvPath.empty()) {
                csvLogger = std::make_unique<telemetry::TraceLogger>(logCsvPath);
                if (!csvLogger->isValid()) {
                    std::cerr << "Failed to open CSV log file: " << logCsvPath << "\n";
                    return false;
                }
                dispatcher.registerConsumer([this](const domain::VehicleSignal& signal) {
                    (*csvLogger)(signal);
                });
            }

            if (!logRawPath.empty()) {
                rawLogger = std::make_unique<telemetry::RawTraceLogger>(logRawPath);
                if (!rawLogger->isValid()) {
                    std::cerr << "Failed to open raw log file: " << logRawPath << "\n";
                    return false;
                }
            }

            // Terminal display. When stdout is carrying CSV data the display
            // moves to stderr, so a downstream pipe receives CSV rows only.
            std::ostream& displayStream = stdoutCsv ? std::cerr : outStream;
            dispatcher.registerConsumer([this, &displayStream](const domain::VehicleSignal& signal) {
                presentation::printTelemetryRow(displayStream, signal, ++dispatchCount_);
            });

            // Additional consumer: decoded CSV rows to the stdout stream. The
            // factory picks the real or the null sink once, so the dispatch
            // loop never re-tests the flag.
            stdoutSink = telemetry::createStdoutSink(
                stdoutCsv, stdoutCsvStream ? *stdoutCsvStream : std::cout);
            if (stdoutCsv) {
                dispatcher.registerConsumer([this](const domain::VehicleSignal& signal) {
                    (*stdoutSink)(signal);
                });
            }

            return true;
        }
    };
}

namespace vehicle_sim::cli {

int TelemetryRunner::run(std::unique_ptr<domain::ISignalSource> source,
                          const domain::VehicleConfig* config,
                          const TelemetryRunOptions& options,
                          const pipeline::StopToken& stop) {
    if (!config) {
        std::cerr << "Vehicle config is null\n";
        return 1;
    }

    // Banners and the run header follow the display stream: with --stdout-csv
    // they must not contaminate the CSV on stdout.
    std::ostream& banner = options.stdoutCsv ? std::cerr : std::cout;
    banner << "\nStarting " << config->vehicleName << " telemetry\n";
    banner << "Press Ctrl+C to stop\n\n";

    presentation::printTelemetryHeader(banner, *config);

    TelemetryPipeline pipeline;
    if (!pipeline.setup(options.logCsvPath, options.logRawPath, std::cout,
                        options.stdoutCsv, options.stdoutCsvStream)) {
        return 1;
    }

    source->start();

    int signalCount = 0;
    auto lastTime = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(options.pollIntervalMs);

    while (!stop.stopRequested()) {
        auto now = std::chrono::steady_clock::now();
        if (auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
            elapsed >= interval) {
            auto signal = source->latestSignal();
            ++signalCount;

            if (pipeline.rawLogger) {
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                // Demo mode uses {0} as placeholder for raw data
                pipeline.rawLogger->write(timestamp, {0});
            }

            pipeline.dispatcher.dispatch(signal);
            lastTime = now;
        }

        // NOTE: Replace busy-wait spin with condition_variable for blocking wait.
        // Current design: unconditional 10ms sleep each iteration.
        // Proposed: std::condition_variable wait notified by requestStop().
        // CONSTRAINT: signalHandler is a C signal handler and CANNOT safely touch
        // mutex/cv (non-async-signal-safe) — it must stay an atomic-flag setter
        // with the loop re-checking. This makes the clean cv design non-trivial.
        std::this_thread::sleep_for(std::chrono::milliseconds(SPIN_SLEEP_MS));
    }

    source->stop();
    banner << "\n\nTelemetry ended. Total signals processed: " << signalCount << "\n";
    banner << "Goodbye!\n";
    return 0;
}

} // namespace vehicle_sim::cli