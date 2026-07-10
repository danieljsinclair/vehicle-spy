#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

#include "vehicle-sim/ble/BLEDeviceInfo.h"

namespace vehicle_sim {

/**
 * @brief Collaborator holding the three BLE callback members and their
 *        invocation. Extracted from BLEManagerBase (cpp:S1448) so the base
 *        transport core no longer owns callback storage or the invoke logic.
 *
 * The device-found / data / connection callbacks are invoked by the other
 * collaborators (DeviceRegistry fires the device-found callback on discovery;
 * ConnectionState fires the connection callback on state change), so those
 * collaborators hold a reference back to this hub.
 */
class CallbackHub {
public:
    using DeviceCallback = std::function<void(const BLEDeviceInfo& device)>;
    using DataCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using ConnectionCallback = std::function<void(bool connected, const std::string& device_id)>;

    /// Store the device-found callback (set by the consumer / facade).
    void setDeviceFoundCallback(DeviceCallback callback) { device_callback_ = std::move(callback); }

    /// Store the raw-data callback (set by the consumer / facade).
    void setDataReceivedCallback(DataCallback callback) { data_callback_ = std::move(callback); }

    /// Store the connection-state callback (set by the consumer / facade).
    void setConnectionCallback(ConnectionCallback callback) { connection_callback_ = std::move(callback); }

    /// Invoke the device-found callback (if set). Thread-safe.
    void invokeDeviceCallback(const BLEDeviceInfo& device) const {
        if (device_callback_) {
            device_callback_(device);
        }
    }

    /// Invoke the raw-data callback (if set). Thread-safe.
    void invokeDataCallback(const std::vector<uint8_t>& data) const {
        if (data_callback_) {
            data_callback_(data);
        }
    }

    /// Invoke the connection-state callback (if set). Thread-safe.
    void invokeConnectionCallback(bool connected, std::string_view device_id) const {
        if (connection_callback_) {
            connection_callback_(connected, std::string(device_id));
        }
    }

private:
    DeviceCallback device_callback_;
    DataCallback data_callback_;
    ConnectionCallback connection_callback_;
};

} // namespace vehicle_sim
