#pragma once

#include <cstdint>

namespace vehicle_sim::domain {

/**
 * Immutable canonical telemetry signal value object
 *
 * Standard output format after physics calculations.
 * This is what gets sent to the dashboard UI.
 * Contains translated RPM, gear, torque values from powertrain model.
 *
 * All fields are guaranteed valid on construction.
 */
class TelemetrySignal final {
public:
    /**
     * Construct a valid TelemetrySignal
     * All parameters are clamped to valid ranges
     */
    TelemetrySignal(
        double rpm,
        int gear,
        double torqueNm,
        double speedKmh,
        double throttlePercent,
        std::uint64_t timestampUtcMs
    ) noexcept;

    // Default copy / move
    TelemetrySignal(const TelemetrySignal&) noexcept = default;
    TelemetrySignal(TelemetrySignal&&) noexcept = default;
    TelemetrySignal& operator=(const TelemetrySignal&) noexcept = default;
    TelemetrySignal& operator=(TelemetrySignal&&) noexcept = default;
    ~TelemetrySignal() noexcept = default;

    // Value accessors
    [[nodiscard]] double getRpm() const noexcept;
    [[nodiscard]] int getGear() const noexcept;
    [[nodiscard]] double getTorqueNm() const noexcept;
    [[nodiscard]] double getSpeedKmh() const noexcept;
    [[nodiscard]] double getThrottlePercent() const noexcept;
    [[nodiscard]] std::uint64_t getTimestampUtcMs() const noexcept;

    // Equality comparison (member-wise)
    [[nodiscard]] friend bool operator==(const TelemetrySignal& lhs, const TelemetrySignal& rhs) noexcept {
        return lhs.m_rpm == rhs.m_rpm &&
               lhs.m_gear == rhs.m_gear &&
               lhs.m_torqueNm == rhs.m_torqueNm &&
               lhs.m_speedKmh == rhs.m_speedKmh &&
               lhs.m_throttlePercent == rhs.m_throttlePercent &&
               lhs.m_timestampUtcMs == rhs.m_timestampUtcMs;
    }

    // Inequality (derived from operator==)
    [[nodiscard]] friend bool operator!=(const TelemetrySignal& lhs, const TelemetrySignal& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    double      m_rpm;             // 0.0 - 12000.0
    int         m_gear;            // -1 (reverse) to 9 (high gear)
    double      m_torqueNm;        // 0.0 - 1500.0
    double      m_speedKmh;        // 0.0 - 400.0
    double      m_throttlePercent; // 0.0 - 100.0
    std::uint64_t m_timestampUtcMs; // Unix timestamp milliseconds
};

} // namespace vehicle_sim::domain
