#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/telemetry/VehicleId.h"

namespace vehicle_sim::telemetry {

/**
 * Canonical CSV telemetry row — matches the --stdout-csv schema used by
 * CSV replay and interactive modes, and rendered by CsvRowFormatter so it is
 * byte-identical to the live --stdout-csv output.
 *
 * Fields correspond to:
 *   timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_light,
 *   acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,
 *   motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count
 */
struct CsvTelemetryRow {
    std::uint64_t timestamp_ms{0};
    VehicleId     vehicle_id;
    double        speed_kmh{0.0};
    double        throttle_percent{0.0};
    std::optional<int> brake_light;  // 1=on, 0=off, nullopt=not reported
    double        acceleration_g{0.0};
    double        steering_angle_deg{0.0};
    double        motor_rpm{0.0};
    double        motor_hv_voltage{0.0};
    double        motor_hv_current{0.0};
    double        motor_torque_nm{0.0};
    GearSelector  gear_selector;
    int           dbc_signal_count{0};
};

} // namespace vehicle_sim::telemetry
