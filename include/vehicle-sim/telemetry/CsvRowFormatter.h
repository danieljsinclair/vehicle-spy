#pragma once

#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/telemetry/CsvCell.h"
#include "vehicle-sim/telemetry/CsvTelemetryRow.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/telemetry/VehicleId.h"

#include <cstdint>
#include <optional>
#include <string>

namespace vehicle_sim::telemetry {

/**
 * Parameters for one CSV data row.
 *
 * Bundles the 13 decoded-telemetry columns into one object so csvRowLine()
 * takes a single parameter (cpp:S107) and the column order lives in exactly
 * one place (DRY). Adding a column = add a field here + one emit line in the
 * sink — no second copy to keep in sync.
 *
 * The four required fields (timestampMs, vehicleId, gearSelector,
 * dbcSignalCount) have no default initialisers: every producer must supply
 * them explicitly. The nine signal fields are optional<double> or
 * optional<int>; a nullopt renders as an empty cell ("not reported").
 */
struct CsvRowParams {
    std::uint64_t         timestampMs;
    VehicleId             vehicleId;
    std::optional<double> speedKmh;
    std::optional<double> throttlePercent;
    std::optional<int>    brakeLight;
    std::optional<double> accelerationG;
    std::optional<double> steeringAngleDeg;
    std::optional<double> motorRpm;
    std::optional<double> motorHvVoltage;
    std::optional<double> motorHvCurrent;
    std::optional<double> motorTorqueNm;
    GearSelector          gearSelector;
    int                   dbcSignalCount;
};

/**
 * Single source of truth for the decoded-telemetry CSV schema (SRP, DRY).
 *
 * Both the file writer (TraceLogger) and the stdout writer (CsvStdoutSink)
 * render rows through this function, so a piped stdout stream and a `--log
 * <base>.csv` file are byte-identical by construction rather than by two
 * hand-maintained copies of the column list.
 *
 * Schema (13 columns):
 *   timestamp_ms, vehicle_id, speed_kmh, throttle_percent, brake_light,
 *   acceleration_g, steering_angle_deg, motor_rpm, motor_hv_voltage,
 *   motor_hv_current, motor_torque_nm, gear_selector, dbc_signal_count
 *
 * Doubles render with two decimal places; absent (nullopt) values render as
 * an empty cell. `brake_light` is a BINARY column: "1"/"0"/empty, never
 * decimal-formatted. `dbc_signal_count` counts the populated fields among
 * the 10 translated signal columns — timestamp and vehicle_id are not counted.
 *
 * Neither function appends a newline — the caller terminates the line.
 */

/// The CSV header line, without a trailing newline.
[[nodiscard]] std::string csvHeaderLine();

/**
 * How many of the 10 translated signal columns are populated in `signal`.
 *
 * Single source of truth for the count (DRY). `timestamp_ms` and `vehicle_id`
 * are NOT counted — only DBC-translated signals.
 */
[[nodiscard]] int countPopulated(const domain::VehicleSignal& signal);

/**
 * One CSV data row for `params`, without a trailing newline — the single
 * renderer for the whole schema.
 *
 * Taint note (cpp:S5145): cfamily's taint is origin-based — it fires on
 * uint64_t and int parameters too, not just std::string. It cannot be cleared
 * by type selection. The sanitization that matters is at the input boundary:
 * GearSelector and VehicleId producers pass through cli::forLog, which
 * replaces control bytes. The S5145 findings are resolved as false positives
 * via SonarCloud marking because cfamily's model cannot observe the cross-TU
 * sanitization. Do NOT attempt to "fix" S5145 by wrapping this output with
 * forLog() — that would corrupt the byte-identical CSV contract.
 */
[[nodiscard]] std::string csvRowLine(const CsvRowParams& params);

} // namespace vehicle_sim::telemetry
