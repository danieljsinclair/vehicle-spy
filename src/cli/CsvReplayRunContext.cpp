// CsvReplayRunContext.cpp - CSV replay emission loop

#include "vehicle-sim/cli/CsvReplayRunContext.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include <chrono>
#include <iostream>

namespace vehicle_sim::cli {

int CsvReplayRunContext::run(
    std::unique_ptr<vehicle_sim::io::CsvTelemetrySource> source,
    const std::string& vehicleId,
    int intervalMs,
    std::ostream& out,
    vehicle_sim::util::IClock& clock,
    bool stdoutCsv)
{
    using namespace vehicle_sim::telemetry;

    if (!source || !source->hasNext()) {
        return 1;
    }

    // When --stdout-csv is set, stdout is a clean pipe: header + rows only.
    // Without it, the rows still go to stdout (this is a bench CSV tool) and a
    // one-line summary is printed to stderr so the stream stays parseable.
    if (stdoutCsv) {
        out << csvHeaderLine() << "\n";
        out.flush();
    } else {
        std::cerr << "Replaying " << source->name() << "\n";
    }

    std::uint64_t prevTs = 0;
    bool first = true;
    std::uint64_t emitted = 0;

    while (source->hasNext()) {
        auto row = source->next();

        // Pace BEFORE emitting each row after the first (never before the
        // first, so replay starts immediately).
        if (!first) {
            if (intervalMs > 0) {
                clock.sleepFor(std::chrono::milliseconds(intervalMs));
            } else {
                // Timestamp-driven: sleep the delta between this row's
                // timestamp_ms and the previous row's. Clamp negative deltas
                // (out-of-order CSV) to zero so replay never sleeps backwards.
                const auto delta = row.timestamp_ms > prevTs
                                       ? (row.timestamp_ms - prevTs)
                                       : 0;
                clock.sleepFor(std::chrono::milliseconds(delta));
            }
        }

        // Allow a vehicle_id override (e.g. --vehicle tesla) to win when the
        // row's own id is blank, so a CSV without a vehicle_id column still
        // produces a labelled stream.
        if (!vehicleId.empty() && row.vehicle_id.empty()) {
            row.vehicle_id = vehicleId;
        }

        // No trailing newline: the stream is header + N rows, terminated by a
        // single newline after the final row so a downstream parser sees
        // exactly N records.
        if (!first) {
            out << "\n";
        }
        out << csvRowLine(row);
        out.flush();
        first = false;
        prevTs = row.timestamp_ms;
        ++emitted;
    }

    if (!stdoutCsv) {
        std::cerr << "Replayed " << emitted << " rows\n";
    }
    return 0;
}

} // namespace vehicle_sim::cli
