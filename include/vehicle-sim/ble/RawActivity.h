#pragma once

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "vehicle-sim/ble/Elm327Session.h"

namespace vehicle_sim {

/**
 * @brief Collaborator tracking raw BLE activity (notification count + last raw
 *        hex) before any parsing. Extracted from BLEManagerBase (cpp:S1448).
 *
 * On each notify(), the count increments, the last-16-byte hex snapshot is
 * refreshed, and the bytes are routed into the composed ELM327 session for
 * parsing (which delivers any parsed binary to the consumer). Behaviour is
 * identical to the prior BLEManagerBase::invokeDataCallback.
 */
class RawActivity {
public:
    explicit RawActivity(Elm327Session& session) : session_(session) {}

    /// Count of raw BLE notifications received (before any parsing).
    [[nodiscard]] int bleNotificationCount() const noexcept { return ble_notification_count_.load(); }

    /// Hex dump of the last raw bytes received (first 16 bytes + "..." if longer).
    [[nodiscard]] std::string lastRawHex() const {
        std::scoped_lock lock(raw_mutex_);
        return last_raw_hex_;
    }

    /// Record a raw notification: bump the counter, snapshot the first 16 bytes
    /// as hex, then route the bytes into the session for parsing/delivery.
    void notify(const std::vector<uint8_t>& data) {
        ble_notification_count_++;
        {
            std::scoped_lock lock(raw_mutex_);
            std::ostringstream hex;
            for (size_t i = 0; i < std::min(data.size(), size_t(16)); ++i) {
                hex << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<int>(data[i]) << " ";
            }
            if (data.size() > 16) hex << "...";
            last_raw_hex_ = hex.str();
        }

        // Route the bytes into the composed ELM327 session: it performs prompt
        // detection (OBD2 mode), CAN/OBD2 parsing, vehicle-detector observation,
        // and delivers any parsed binary to the consumer via sessionDeliverParsed.
        // Raw bytes are not delivered directly — only parsed binary — matching
        // the prior BLEManagerBase behaviour.
        session_.handleIncomingData(data);
    }

private:
    mutable std::mutex raw_mutex_;       // guards last_raw_hex_
    std::string last_raw_hex_;
    std::atomic<int> ble_notification_count_{0};
    Elm327Session& session_;
};

} // namespace vehicle_sim
