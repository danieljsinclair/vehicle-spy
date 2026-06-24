#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/presentation/VehicleSignalFormatter.h"
#include "vehicle-sim/domain/EventDispatcher.h"
#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/RawTraceLogger.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>

namespace {
    std::atomic<bool> g_running(true);
    constexpr int SPIN_SLEEP_MS = 10;

    void signalHandler(int sigNum) {
        std::cout << "\nReceived signal " << sigNum << ", shutting down..." << std::endl;
        g_running = false;
    }

    struct TelemetryPipeline {
        vehicle_sim::domain::EventDispatcher dispatcher;
        std::unique_ptr<vehicle_sim::telemetry::TraceLogger> csvLogger;
        std::unique_ptr<vehicle_sim::telemetry::RawTraceLogger> rawLogger;
        int dispatchCount_ = 0;

        bool setup(const std::string& logCsvPath,
                   const std::string& logRawPath,
                   std::ostream& outStream) {
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

            dispatcher.registerConsumer([this, &outStream](const domain::VehicleSignal& signal) {
                presentation::printTelemetryRow(outStream, signal, ++dispatchCount_);
            });

            return true;
        }
    };
}

namespace vehicle_sim::cli {

int TelemetryRunner::run(std::unique_ptr<domain::ISignalSource> source,
                          const domain::VehicleConfig* config,
                          const std::string& logCsvPath,
                          const std::string& logRawPath,
                          int pollIntervalMs) {
    if (!config) {
        std::cerr << "Vehicle config is null\n";
        return 1;
    }

    std::cout << "\nStarting " << config->vehicleName << " telemetry\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    presentation::printTelemetryHeader(std::cout, *config);

    TelemetryPipeline pipeline;
    if (!pipeline.setup(logCsvPath, logRawPath, std::cout)) {
        return 1;
    }

    source->start();

    int signalCount = 0;
    auto lastTime = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(pollIntervalMs);

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);

        if (elapsed >= interval) {
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
    std::cout << "\n\nTelemetry ended. Total signals processed: " << signalCount << "\n";
    std::cout << "Goodbye!\n";
    return 0;
}

// Register signal handlers (called from main)
void registerSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

void TelemetryRunner::resetRunningState() {
    g_running = true;
}

void TelemetryRunner::requestStop() {
    g_running = false;
}

} // namespace vehicle_sim::cli