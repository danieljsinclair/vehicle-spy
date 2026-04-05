#pragma once

#include <vector>
#include <functional>
#include <string>
#include <cstdint>
#include "BLEDeviceInfo.h"

namespace vehicle_sim {

/**
 * @brief Pure virtual platform abstraction for BLE operations.
 *
 * Platform-specific implementations (iOS CoreBluetooth, Linux BlueZ, Mock)
 * must implement this interface. BLEManager delegates all BLE work to
 * a platform implementation selected at runtime.
 */
class BLEPlatform {
public:
    using DeviceCallback = std::function<void(const BLEDeviceInfo& device)>;
    using DataCallback = std::function<void(const std::vector<uint8_t>& data)>;

    virtual ~BLEPlatform() = default;

    // Scan for BLE devices within range
    virtual std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds) = 0;

    // Connect to a specific device by address (MAC on Linux, UUID on iOS)
    virtual bool connect(const std::string& device_identifier) = 0;

    // Disconnect from current device
    virtual void disconnect() = 0;

    // Subscribe to device discovery events
    virtual void setDeviceFoundCallback(DeviceCallback callback) = 0;

    // Subscribe to raw BLE data notifications
    virtual void setDataReceivedCallback(DataCallback callback) = 0;

    // Check connection status
    virtual bool isConnected() const = 0;

    // Get the identifier of the currently connected device (if any)
    virtual std::string getConnectedDeviceId() const = 0;
};

} // namespace vehicle_sim
