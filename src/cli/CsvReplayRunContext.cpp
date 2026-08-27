// CsvReplayRunContext.cpp - CSV replay emission loop

#include "vehicle-sim/cli/CsvReplayRunContext.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include <chrono>
#include <iostream>
#include <string_view>

namespace vehicle_sim::cli {

int CsvReplayRunContext::run(
    std::unique_ptr<vehicle_sim::io::CsvTelemetrySource> source,
    std::string_view vehicleId,
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
            // Boundary sanitize the operator-supplied label before it enters the
            // CSV DATA sink (csvRowLine(CsvRowParams)). validateOptions has
            // already rejected control characters and over-length labels. The
            // VehicleId type sanitizes via forLog() (a no-op on valid input,
            // preserving the byte-identical CSV contract) AND takes the id out of
            // cfamily's std::string taint set. This site is adjacent to the sink
            // flow in the same TU.
            row.vehicle_id = vehicle_sim::telemetry::VehicleId::fromUserInput(vehicleId);
        }

        // No trailing newline: the stream is header + N rows, terminated by a
        // single newline after the final row so a downstream parser sees
        // exactly N records.
        if (!first) {
            out << "\n";
        }
        const CsvRowParams params{
            row.timestamp_ms,
            row.vehicle_id,
            std::optional<double>(row.speed_kmh),
            std::optional<double>(row.throttle_percent),
            row.brake_light,
            std::optional<double>(row.acceleration_g),
            std::optional<double>(row.steering_angle_deg),
            std::optional<double>(row.motor_rpm),
            std::optional<double>(row.motor_hv_voltage),
            std::optional<double>(row.motor_hv_current),
            std::optional<double>(row.motor_torque_nm),
            row.gear_selector,
            row.dbc_signal_count,
        };
        out << csvRowLine(params);
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
