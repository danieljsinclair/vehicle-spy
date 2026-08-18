#pragma once

#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/telemetry/CsvTelemetryRow.h"

#include <string>

namespace vehicle_sim::telemetry {

/**
 * Single source of truth for the decoded-telemetry CSV schema (SRP, DRY).
 *
 * Both the file writer (TraceLogger, behind DecodedCsvSink) and the stdout
 * writer (CsvStdoutSink) render rows through these functions, so a piped
 * stdout stream and a `--log <base>.csv` file are byte-identical by
 * construction rather than by two hand-maintained copies of the column list.
 *
 * Schema (13 columns):
 *   timestamp_ms, vehicle_id, speed_kmh, throttle_percent, brake_light,
 *   acceleration_g, steering_angle_deg, motor_rpm, motor_hv_voltage,
 *   motor_hv_current, motor_torque_nm, gear_selector, dbc_signal_count
 *
 * Neither function appends a newline — the caller terminates the line.
 */

/// The CSV header line, without a trailing newline.
[[nodiscard]] std::string csvHeaderLine();

/**
 * One CSV data row for `signal`, without a trailing newline.
 *
 * Doubles render with two decimal places; absent (nullopt) values render as
 * an empty cell. `brake_light` is a BINARY column: "1"/"0"/empty, never
 * decimal-formatted. `gear_selector` renders as its display label (e.g. 4097 →
 * "D"), falling back to the raw number for unmapped values.
 * `dbc_signal_count` counts the populated fields among the 10 translated
 * signal columns — timestamp and vehicle_id are not counted.
 *
 * @param signal    Decoded signal to render.
 * @param vehicleId Value for the `vehicle_id` column; "" leaves it blank.
 */
[[nodiscard]] std::string csvRowLine(const domain::VehicleSignal& signal,
                                     const std::string& vehicleId);

/**
 * One CSV data row for a `CsvTelemetryRow`, without a trailing newline.
 *
 * Overload for the bench-replay / interactive telemetry path (CSV replay and
 * interactive modes). Renders the same 13-column schema as the
 * `VehicleSignal` overload so a piped stdout stream is byte-identical to the
 * live `--stdout-csv` output (single source of truth, DRY).
 *
 * @param row       Pre-computed telemetry row to render.
 */
[[nodiscard]] std::string csvRowLine(const CsvTelemetryRow& row);

} // namespace vehicle_sim::telemetry
