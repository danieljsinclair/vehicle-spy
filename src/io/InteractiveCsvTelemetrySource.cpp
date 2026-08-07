// InteractiveCsvTelemetrySource.cpp - Keyboard-driven telemetry source

#include "vehicle-sim/io/InteractiveCsvTelemetrySource.h"

#include <algorithm>
#include <chrono>

namespace vehicle_sim::io {

InteractiveCsvTelemetrySource::InteractiveCsvTelemetrySource(
    std::unique_ptr<vehicle_sim::interactive::KeyboardThrottle> throttle,
    vehicle_sim::util::IClock& clock,
    std::string vehicleId,
    int intervalMs
)
    : m_throttle{std::move(throttle)}
    , m_clock{clock}
    , m_vehicleId{std::move(vehicleId)}
    , m_intervalMs{intervalMs > 0 ? intervalMs : 20}
{
}

bool InteractiveCsvTelemetrySource::hasNext() const {
    return !m_quit;
}

vehicle_sim::telemetry::CsvTelemetryRow InteractiveCsvTelemetrySource::next() {
    if (!m_started) {
        m_started = true;
    } else {
        // Throttle to the configured tick rate in production. With an injected
        // FakeClock this is instant (no real-time sleep) so tests stay
        // deterministic and wall-clock-free.
        m_clock.sleepFor(std::chrono::milliseconds(m_intervalMs));
    }

    auto state = m_throttle->poll();
    m_quit = state.quit;

    // Advance the deterministic simulated timestamp by the tick interval.
    m_timestampMs += static_cast<std::uint64_t>(m_intervalMs);

    vehicle_sim::telemetry::CsvTelemetryRow row;
    row.timestamp_ms       = m_timestampMs;
    row.vehicle_id         = m_vehicleId;
    row.throttle_percent   = state.throttle_percent;
    row.gear_selector      = std::to_string(state.gear);
    row.brake_percent      = state.brake_percent;
    row.steering_angle_deg = state.steering_angle_deg;

    // Derived fields — simple bench model, not physics-accurate.
    // Speed: linear with throttle, capped; braking trims it.
    row.speed_kmh = state.throttle_percent * 4.0;   // 100% -> 400 km/h
    if (state.brake_percent > 0.0) {
        row.speed_kmh = std::max(0.0, row.speed_kmh - state.brake_percent * 2.0);
    }

    // Acceleration: proportional to throttle, negative when braking.
    row.acceleration_g = (state.throttle_percent / 100.0) * 0.5;
    if (state.brake_percent > 0.0) {
        row.acceleration_g -= 0.3;
    }

    // Motor RPM: speed / gear, floored at idle.
    const double gearRatio = (state.gear > 0) ? static_cast<double>(state.gear) : 1.0;
    row.motor_rpm = (row.speed_kmh / gearRatio) * 100.0;
    if (row.motor_rpm < 800.0) row.motor_rpm = 800.0;

    // HV system: constant voltage, current follows throttle.
    row.motor_hv_voltage = 400.0;
    row.motor_hv_current = state.throttle_percent * 2.0;   // 100% -> 200 A

    // Torque: simplified linear model.
    row.motor_torque_nm = state.throttle_percent * 15.0;

    // DBC signal count — constant for bench output.
    row.dbc_signal_count = 42;

    return row;
}

} // namespace vehicle_sim::io
