// FileCsvTelemetrySource.cpp - Decoded-CSV file reader for replay mode

#include "vehicle-sim/io/FileCsvTelemetrySource.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"
#include "vehicle-sim/telemetry/GearSelector.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace vehicle_sim::io {

namespace {
std::string trim(std::string_view s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const size_t end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

std::vector<std::string> splitCsv(std::string_view line) {
    std::vector<std::string> fields;
    std::string lineStr(line);
    std::stringstream ss(lineStr);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

int findColumn(const std::vector<std::string>& headers, std::string_view name) {
    auto it = std::find_if(headers.begin(), headers.end(),
        [&](const std::string& h) { return h == name; });
    return it != headers.end() ? static_cast<int>(std::distance(headers.begin(), it)) : -1;
}

double toDouble(std::string_view s, double fallback) {
    try {
        size_t idx;
        const double v = std::stod(std::string(s), &idx);
        return idx > 0 ? v : fallback;
    } catch (const std::invalid_argument&) {
        return fallback;
    } catch (const std::out_of_range&) {
        return fallback;
    }
}

int toInt(std::string_view s, int fallback) {
    try {
        size_t idx;
        const int v = std::stoi(std::string(s), &idx);
        return idx > 0 ? v : fallback;
    } catch (const std::invalid_argument&) {
        return fallback;
    } catch (const std::out_of_range&) {
        return fallback;
    }
}

// Blank or unparseable cells are "not reported" (nullopt) — distinct from a
// definite 0. Mirrors the brake-light column's tri-state contract.
std::optional<int> toOptionalInt(std::string_view s) {
    try {
        size_t idx;
        const int v = std::stoi(std::string(s), &idx);
        if (idx == 0) return std::nullopt;
        return v;
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}
} // anonymous

FileCsvTelemetrySource::FileCsvTelemetrySource(std::string filePath)
    : m_filePath{std::move(filePath)}
{
    m_in.open(m_filePath);
    if (!m_in.is_open()) {
        throw domain::TelemetryFileException(m_filePath);
    }

    // Read and parse header (column order is irrelevant; names are matched).
    std::string headerLine;
    if (!std::getline(m_in, headerLine)) {
        throw domain::TelemetryFileException(m_filePath);
    }
    m_headers = splitCsv(headerLine);

    m_col.timestamp_ms       = findColumn(m_headers, "timestamp_ms");
    m_col.vehicle_id         = findColumn(m_headers, "vehicle_id");
    m_col.speed_kmh          = findColumn(m_headers, "speed_kmh");
    m_col.throttle_percent   = findColumn(m_headers, "throttle_percent");
    m_col.brake_light        = findColumn(m_headers, "brake_light");
    m_col.acceleration_g     = findColumn(m_headers, "acceleration_g");
    m_col.steering_angle_deg = findColumn(m_headers, "steering_angle_deg");
    m_col.motor_rpm          = findColumn(m_headers, "motor_rpm");
    m_col.motor_hv_voltage   = findColumn(m_headers, "motor_hv_voltage");
    m_col.motor_hv_current   = findColumn(m_headers, "motor_hv_current");
    m_col.motor_torque_nm    = findColumn(m_headers, "motor_torque_nm");
    m_col.gear_selector      = findColumn(m_headers, "gear_selector");
    m_col.dbc_signal_count   = findColumn(m_headers, "dbc_signal_count");
}

bool FileCsvTelemetrySource::hasNext() const {
    // Look ahead exactly one line so the caller never gets a phantom
    // end-of-file row. If we haven't yet peeked and the stream is still
    // readable, pull the next non-empty line into the pending buffer.
    if (m_eof) return false;
    if (!m_pending.has_value()) {
        std::string line;
        while (std::getline(m_in, line)) {
            if (!line.empty()) {
                m_pending = std::move(line);
                break;
            }
        }
        if (!m_pending.has_value()) {
            m_eof = true;
            return false;
        }
    }
    return true;
}

bool FileCsvTelemetrySource::parseRow(std::string_view line,
                                      vehicle_sim::telemetry::CsvTelemetryRow& out) const {
    auto fields = splitCsv(line);
    if (fields.empty()) return false;

    if (fields.size() < m_headers.size()) {
        fields.resize(m_headers.size());
    }

    const auto get = [&](int col, double fallback) {
        return (col >= 0 && col < static_cast<int>(fields.size()))
                   ? toDouble(fields[col], fallback)
                   : fallback;
    };
    const auto getInt = [&](int col, int fallback) {
        return (col >= 0 && col < static_cast<int>(fields.size()))
                   ? toInt(fields[col], fallback)
                   : fallback;
    };
    const auto getStr = [&](int col) {
        return (col >= 0 && col < static_cast<int>(fields.size()))
                   ? fields[col]
                   : std::string{};
    };
    const auto getOptInt = [&](int col) {
        return (col >= 0 && col < static_cast<int>(fields.size()))
                   ? toOptionalInt(fields[col])
                   : std::optional<int>{};
    };

    out.timestamp_ms       = static_cast<std::uint64_t>(get(m_col.timestamp_ms, 0));
    // The file-derived vehicle_id is genuinely external; VehicleId::fromUserInput
    // sanitizes it (control bytes -> '?') and, by making vehicle_id a distinct
    // type, takes it out of cfamily's std::string taint set so the CSV DATA sink
    // is clean. gear_selector is equally external (also a file-derived string)
    // and is validated the SAME way via GearSelector — both distinct types
    // remove their fields from cfamily's std::string taint set. The remaining
    // row fields are numeric.
    out.vehicle_id         = vehicle_sim::telemetry::VehicleId::fromUserInput(getStr(m_col.vehicle_id));
    out.gear_selector      = vehicle_sim::telemetry::GearSelector::fromUserInput(getStr(m_col.gear_selector));
    out.speed_kmh          = get(m_col.speed_kmh, 0.0);
    out.throttle_percent   = get(m_col.throttle_percent, 0.0);
    out.brake_light        = getOptInt(m_col.brake_light);
    out.acceleration_g     = get(m_col.acceleration_g, 0.0);
    out.steering_angle_deg = get(m_col.steering_angle_deg, 0.0);
    out.motor_rpm          = get(m_col.motor_rpm, 0.0);
    out.motor_hv_voltage   = get(m_col.motor_hv_voltage, 0.0);
    out.motor_hv_current   = get(m_col.motor_hv_current, 0.0);
    out.motor_torque_nm    = get(m_col.motor_torque_nm, 0.0);
    out.dbc_signal_count   = getInt(m_col.dbc_signal_count, 0);

    return true;
}

vehicle_sim::telemetry::CsvTelemetryRow FileCsvTelemetrySource::next() {
    // Self-prime: ensure a pending line is buffered even if the caller did
    // not call hasNext() first (robust against any caller of the
    // CsvTelemetrySource interface). hasNext() is const but mutates the
    // mutable look-ahead buffers, so it is safe to call here.
    if (!m_pending.has_value()) {
        hasNext();
    }
    vehicle_sim::telemetry::CsvTelemetryRow row;
    if (m_pending.has_value()) {
        parseRow(*m_pending, row);
        m_pending.reset();
    }
    return row;
}

} // namespace vehicle_sim::io
