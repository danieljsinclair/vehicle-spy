#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace vehicle_sim::ble {

/**
 * Pure virtual interface for low-level BLE transport
 *
 * This is the lowest layer in the boundary stack:
 * ITransport → SignalTranslator → VehicleSignal
 *
 * Concrete implementations will use platform-specific BLE APIs
 * (CoreBluetooth on macOS/iOS, BlueZ on Linux)
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * Connect to a BLE device
     * @param address Device address (MAC or UUID depending on platform)
     * @return true if connection successful
     */
    virtual bool connect(const std::string& address) = 0;

    /**
     * Disconnect from current device
     */
    virtual void disconnect() = 0;

    /**
     * Check if connected to a device
     */
    virtual bool isConnected() const = 0;

    /**
     * Send data to connected device
     * @param data Raw bytes to send
     */
    virtual void send(const std::vector<std::uint8_t>& data) = 0;

    /**
     * Subscribe to incoming data notifications
     * @param callback Function to call when data is received
     */
    virtual void subscribeToNotifications(
        std::function<void(const std::vector<std::uint8_t>&)> callback
    ) = 0;
};

} // namespace vehicle_sim::ble
