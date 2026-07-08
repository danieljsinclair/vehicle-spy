#pragma once

#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/telemetry/TraceLogger.h"
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Writes the decoded signal stream to "<base>.csv" — the derived, decoded
 * view of a capture. The schema is the existing 11-column telemetry layout
 * (timestamp + 10 signal columns); the underlying TraceLogger is reused so
 * output stays byte-identical to the legacy --log-csv path. Phase 3 may
 * expand the schema to 12 columns.
 *
 * Raw capture is the source of truth; this CSV is derived from it.
 *
 * Rule of Zero: holds TraceLogger by value, so the compiler-generated special
 * members give correct move-only semantics (TraceLogger/std::ofstream are
 * non-copyable, movable). No hand-written dtor or copy/move ops needed.
 */
class DecodedCsvSink final {
public:
    /**
     * Open <base>.csv for writing. Throws std::runtime_error if the file
     * cannot be created (matches TraceLogger's contract) — callers should
     * construct inside a try/catch at the run-context boundary. The optional
     * vehicleId is threaded into the CSV's `vehicle_id` column; default ""
     * keeps existing single-arg callers compiling.
     */
    explicit DecodedCsvSink(const std::string& base, const std::string& vehicleId = "");

    void write(const domain::VehicleSignal& signal) noexcept;
    [[nodiscard]] bool isValid() const noexcept;

private:
    telemetry::TraceLogger logger_;
};

} // namespace vehicle_sim::pipeline
