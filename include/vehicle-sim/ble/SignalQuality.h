#pragma once

#include <string>

namespace vehicle_sim {

// RSSI thresholds (dBm) for signal-quality classification.
constexpr int RSSI_EXCELLENT = -50;
constexpr int RSSI_GOOD = -65;
constexpr int RSSI_FAIR = -75;

/**
 * Convert RSSI to a human-readable signal quality string.
 * @param rssi Signal strength in dBm
 * @return "Excellent" / "Good" / "Fair" / "Poor"
 */
[[nodiscard]] inline std::string signalQuality(int rssi) {
    if (rssi >= RSSI_EXCELLENT) return "Excellent";
    if (rssi >= RSSI_GOOD) return "Good";
    if (rssi >= RSSI_FAIR) return "Fair";
    return "Poor";
}

} // namespace vehicle_sim
