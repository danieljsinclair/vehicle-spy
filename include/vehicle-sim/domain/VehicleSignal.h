#pragma once

#include <cstdint>

namespace vehicle_sim::domain {

/**
 * Immutable canonical vehicle signal value object
 *
 * Standard OBD2 normalized format. All values are in standard units.
 * This is the only signal format that crosses the boundary layer.
 * No Tesla specific values, units or encodings exist in this type.
 *
 * All fields are guaranteed valid on construction.
 */
class VehicleSignal final {
public:
    /**
     * Construct a valid VehicleSignal
     * All parameters are clamped to valid ranges
     */
    VehicleSignal(
        double throttlePercent,
        double speedKmh,
        double accelerationG,
        double brakePercent,
        std::uint64_t timestampUtcMs
    ) noexcept;

    // Default copy / move
    VehicleSignal(const VehicleSignal&) noexcept = default;
    VehicleSignal(VehicleSignal&&) noexcept = default;
    VehicleSignal& operator=(const VehicleSignal&) noexcept = default;
    VehicleSignal& operator=(VehicleSignal&&) noexcept = default;
    ~VehicleSignal() noexcept = default;

    // Value accessors
    [[nodiscard]] double getThrottlePercent() const noexcept;
    [[nodiscard]] double getSpeedKmh() const noexcept;
    [[nodiscard]] double getAccelerationG() const noexcept;
    [[nodiscard]] double getBrakePercent() const noexcept;
    [[nodiscard]] std::uint64_t getTimestampUtcMs() const noexcept;

    // Equality comparison (member-wise)
    [[nodiscard]] bool operator==(const VehicleSignal& other) const noexcept;

    // Inequality (derived from operator==)
    [[nodiscard]] bool operator!=(const VehicleSignal& other) const noexcept;

private:
    double      m_throttlePercent;  // 0.0 - 100.0
    double      m_speedKmh;         // 0.0 - 300.0
    double      m_accelerationG;    // -5.0 to +5.0
    double      m_brakePercent;     // 0.0 - 100.0
    std::uint64_t m_timestampUtcMs; // Unix timestamp milliseconds
};

} // namespace vehicle_sim::domain
