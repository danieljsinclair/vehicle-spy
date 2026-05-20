#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include <string>
#include <memory>

namespace vehicle_sim::cli {

/**
 * Register signal handlers (SIGINT, SIGTERM)
 *
 * Must be called from main() before creating the ISignalSource.
 */
void registerSignalHandlers();

/**
 * Unified telemetry runner
 *
 * Consolidates the main loop for all telemetry sources (demo, BLE, etc.).
 * Takes an ISignalSource via dependency injection and polls latestSignal()
 * at the specified interval.
 *
 * DI: The source is injected, not constructed internally.
 * OCP: New data sources are added by creating new ISignalSource implementations.
 */
class TelemetryRunner {
public:
    /**
     * Run telemetry with the given signal source
     *
     * @param source Unique pointer to signal source (takes ownership)
     * @param config Vehicle configuration (may be null)
     * @param logCsvPath Path for CSV log (empty to disable)
     * @param logRawPath Path for raw log (empty to disable)
     * @param pollIntervalMs Polling interval in milliseconds
     * @return Exit code (0 on success, non-zero on error)
     */
    static int run(std::unique_ptr<domain::ISignalSource> source,
                   const domain::VehicleConfig* config,
                   const std::string& logCsvPath,
                   const std::string& logRawPath,
                   int pollIntervalMs);

    static void resetRunningState();

    static void requestStop();
};

} // namespace vehicle_sim::cli