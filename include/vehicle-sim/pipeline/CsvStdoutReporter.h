#pragma once

#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/telemetry/CsvStdoutSink.h"

#include <cstddef>

namespace vehicle_sim::pipeline {

/**
 * Adapts an ICsvStdoutSink onto the replay loop's IProgressReporter seam.
 *
 * This is how --stdout-csv reaches the canonical pipeline without changing
 * runReplay(): the loop already notifies an IProgressReporter once per decoded
 * frame, so emitting CSV is just another implementation of that interface
 * (Open/Closed). The sink owns the CSV formatting (SRP); this class owns only
 * the adaptation.
 *
 * onComplete() is deliberately a no-op — a CSV stream ends with its last data
 * row, and a trailing summary would corrupt it for any downstream parser.
 */
class CsvStdoutReporter final : public IProgressReporter {
public:
    explicit CsvStdoutReporter(telemetry::ICsvStdoutSink& sink) noexcept;

    void onFrame(
        const domain::VehicleSignal& signal,
        std::size_t frameIndex,
        std::size_t totalHints
    ) noexcept override;

    void onComplete(const ReplayStats& stats) noexcept override;

private:
    telemetry::ICsvStdoutSink& sink_;
};

} // namespace vehicle_sim::pipeline
