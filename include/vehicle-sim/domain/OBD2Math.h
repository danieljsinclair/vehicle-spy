#pragma once

#include <cstdint>

namespace vehicle_sim::domain {

// OBD2 PID scaling formulas per SAE J1979
// Single source of truth — use these everywhere OBD2 data is decoded.

// Single byte → percentage: (A / 255) * 100
inline double obd2BytePercent(std::uint8_t byte) noexcept {
    return (static_cast<double>(byte) / 255.0) * 100.0;
}

// Two bytes → RPM: ((A * 256) + B) / 4
inline double obd2WordRPM(std::uint8_t msb, std::uint8_t lsb) noexcept {
    return (static_cast<double>(msb) * 256.0 + static_cast<double>(lsb)) / 4.0;
}

// Single byte → Celsius: A - 40
inline double obd2TempCelsius(std::uint8_t byte) noexcept {
    return static_cast<double>(byte) - 40.0;
}

// Raw byte value (speed, etc.): A as-is
inline double obd2RawValue(std::uint8_t byte) noexcept {
    return static_cast<double>(byte);
}

} // namespace vehicle_sim::domain
