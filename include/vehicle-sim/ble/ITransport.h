#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <optional>

namespace vehicle_sim::ble {

/**
 * Pure virtual interface for low-level BLE transport
 *
 * This is the lowest layer in the boundary stack:
 * ITransport → SignalTranslator → VehicleSignal
 *
 * Concrete implementations will use platform-specific BLE APIs
 * (CoreBluetooth on macOS/iOS, BlueZ on Linux)
 *
 * Thread-safe: All methods must be safe for concurrent calls
 * from multiple threads.
 *
 * Single Responsibility: Only handles raw BLE communication.
 * No protocol parsing, signal extraction, or data transformation.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * Connect to a BLE device
     * @param address Device address (MAC or UUID depending on platform)
     * @return true if connection successful
     * Thread-safe: May be called from any thread
     */
    virtual bool connect(const std::string& address) = 0;

    /**
     * Disconnect from current device
     * Thread-safe: May be called from any thread
     */
    virtual void disconnect() = 0;

    /**
     * Check if connected to a device
     * Thread-safe: May be called from any thread
     */
    virtual bool isConnected() const = 0;

    /**
     * Read data from connected device
     * @return Optional vector of bytes, empty if no data available
     * Thread-safe: May be called from any thread
     */
    virtual std::optional<std::vector<std::uint8_t>> readData() = 0;

    /**
     * Send data to connected device
     * @param data Raw bytes to send
     * Thread-safe: May be called from any thread
     */
    virtual void send(const std::vector<std::uint8_t>& data) = 0;

    /**
     * Subscribe to incoming data notifications
     * @param callback Function to call when data is received
     * Thread-safe: May be called from any thread
     * Callback is invoked on internal thread - caller must ensure thread safety
     */
    virtual void subscribeToNotifications(
        std::function<void(const std::vector<std::uint8_t>&)> callback
    ) = 0;

    /**
     * Unsubscribe from incoming data notifications
     * Thread-safe: May be called from any thread
     */
    virtual void unsubscribe() = 0;
};

} // namespace vehicle_sim::ble
