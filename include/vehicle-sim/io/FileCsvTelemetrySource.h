#pragma once

#include "vehicle-sim/io/CsvTelemetrySource.h"

#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace vehicle_sim::io {

/**
 * File-backed telemetry source for decoded-CSV replay.
 *
 * Reads a CSV with the decoded-telemetry schema (header row required), and
 * yields one CsvTelemetryRow per data row. Column order is irrelevant — the
 * header names are matched; missing columns default to 0 / empty. This is a
 * PURE reader: it performs no timing, sleeping, or pacing. Pacing
 * (timestamp-driven or fixed --interval) is the concern of the run context,
 * which keeps this class deterministic and testable.
 *
 * Schema (header names matched case-sensitively):
 *   timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_light,
 *   acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,
 *   motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count
 *
 * Old captures carrying a brake_percent column still load: unknown columns
 * are ignored, and brake_light then stays nullopt for every row.
 */
class FileCsvTelemetrySource final : public CsvTelemetrySource {
public:
    /**
     * @param filePath Path to the decoded telemetry CSV.
     * @throws std::runtime_error if the file cannot be opened or is empty.
     */
    explicit FileCsvTelemetrySource(std::string filePath);

    ~FileCsvTelemetrySource() override = default;

    bool hasNext() const override;
    vehicle_sim::telemetry::CsvTelemetryRow next() override;
    std::string name() const override { return m_filePath; }

private:
    struct ColumnMap {
        int timestamp_ms{-1};
        int vehicle_id{-1};
        int speed_kmh{-1};
        int throttle_percent{-1};
        int brake_light{-1};
        int acceleration_g{-1};
        int steering_angle_deg{-1};
        int motor_rpm{-1};
        int motor_hv_voltage{-1};
        int motor_hv_current{-1};
        int motor_torque_nm{-1};
        int gear_selector{-1};
        int dbc_signal_count{-1};
    };

    bool parseRow(std::string_view line, vehicle_sim::telemetry::CsvTelemetryRow& out) const;

    // Look-ahead buffering: m_pending holds the next real data row (when
    // engaged) and m_eof marks true end-of-stream. hasNext() pulls the next
    // non-empty line into m_pending exactly once, so next() only ever returns
    // a parsed row (never a trailing EOF sentinel) and a consumer loop driven
    // by hasNext() never emits a spurious blank record.
    mutable std::ifstream    m_in;
    std::string              m_filePath;
    mutable bool             m_eof{false};
    mutable std::optional<std::string> m_pending;   // buffered next data line
    std::vector<std::string> m_headers;
    ColumnMap                m_col;
};

} // namespace vehicle_sim::io
