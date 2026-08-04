#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include <string>
#include <memory>
#include <iosfwd>

namespace vehicle_sim::cli {

/**
 * Configuration bundle for a telemetry run (cpp:S107 parameter object).
 *
 * Groups the run's tunables — log paths, poll cadence, and the --stdout-csv
 * behaviour — into one object so TelemetryRunner::run() stays below the S107
 * parameter-count threshold and call sites read by name. Mirrors the
 * VehicleSignal::Params precedent already used elsewhere in the codebase.
 */
struct TelemetryRunOptions {
    /// Path for the decoded CSV log ("<base>.csv"); empty disables it.
    std::string logCsvPath;
    /// Path for the raw capture log; empty disables it.
    std::string logRawPath;
    /// Polling interval in milliseconds; the caller sets this per run.
    int pollIntervalMs = 0;
    /// Emit decoded CSV rows to stdout (same schema as <base>.csv); moves the
    /// human-readable display to stderr so the stdout stream stays pipeable.
    bool stdoutCsv = false;
    /// Destination for the stdout CSV rows; nullptr means std::cout. Injected
    /// so tests capture the stream without touching the real process stdout.
    std::ostream* stdoutCsvStream = nullptr;
};

/**
 * Unified telemetry runner
 *
 * Consolidates the main loop for all telemetry sources (demo, BLE, etc.).
 * Takes an ISignalSource via dependency injection and polls latestSignal()
 * at the specified interval.
 *
 * DI: The source is injected, not constructed internally.
 * OCP: New data sources are added by creating new ISignalSource implementations.
 *
 * The cooperative stop is an injected StopToken (shared with the caller's signal
 * handler via SignalStopBroker) — no process-global flag.
 */
class TelemetryRunner {
public:
    /**
     * Run telemetry with the given signal source
     *
     * @param source  Unique pointer to signal source (takes ownership).
     * @param config  Vehicle configuration (may be null).
     * @param options Run configuration (log paths, poll interval, --stdout-csv).
     * @param stop    Cooperative stop token; the loop ends when stop.requested().
     * @return Exit code (0 on success, non-zero on error).
     */
    static int run(std::unique_ptr<domain::ISignalSource> source,
                   const domain::VehicleConfig* config,
                   const TelemetryRunOptions& options,
                   const pipeline::StopToken& stop);
};

} // namespace vehicle_sim::cli