#pragma once

#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include <string>
#include <functional>
#include <memory>

namespace vehicle_sim::cli {

/**
 * BLE connection lifecycle manager
 *
 * Handles discovery, protocol initialization, and health monitoring for BLE OBD2 adapters.
 * Provides a configured BLE connection with data callbacks.
 *
 * Separation of concerns: main() should not contain BLE protocol logic,
 * service enumeration, or health checks.
 */
class BLEConnectionManager {
public:
    using DataCallback = std::function<void(const std::vector<std::uint8_t>& data)>;

    explicit BLEConnectionManager(std::unique_ptr<BLEManager> bleManager) noexcept;
    ~BLEConnectionManager();

    BLEConnectionManager(const BLEConnectionManager&) = delete;
    BLEConnectionManager& operator=(const BLEConnectionManager&) = delete;

    /**
     * Connect to a BLE device by address
     *
     * @param address BLE device address (e.g., "AA:BB:CC:DD:EE:FF")
     * @param protocol Vehicle protocol to initialize (CAN or OBD2)
     * @param callback Callback for received data
     * @return true if connection successful, false otherwise
     */
    [[nodiscard]] bool connect(const std::string& address,
                                domain::VehicleProtocol protocol,
                                const DataCallback& callback);

    /**
     * Start data polling
     *
     * @param updateIntervalMs Polling interval in milliseconds
     */
    void startPolling(int updateIntervalMs);

    /**
     * Stop data polling
     */
    void stopPolling();

    /**
     * Disconnect from BLE device
     */
    void disconnect();

    /**
     * Check if connected
     */
    [[nodiscard]] bool isConnected() const;

    /**
     * Get BLE notification count (for status display)
     */
    [[nodiscard]] int notificationCount() const;

    /**
     * Get last raw hex data (for status display)
     */
    [[nodiscard]] std::string lastRawHex() const;

    /**
     * Access to VehicleDetector (for diagnostics)
     */
    [[nodiscard]] const domain::VehicleDetector* vehicleDetector() const;

    /**
     * Check if connection was lost
     */
    [[nodiscard]] bool connectionLost() const;

private:
    std::unique_ptr<BLEManager> bleManager_;
    bool isConnected_{false};
    bool polling_{false};
    domain::VehicleProtocol protocol_{domain::VehicleProtocol::OBD2};
};

} // namespace vehicle_sim::cli