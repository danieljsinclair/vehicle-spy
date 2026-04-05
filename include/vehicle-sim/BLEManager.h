#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "ble/BLEPlatform.h"
#include "ble/BLEDeviceInfo.h"

namespace vehicle_sim {

class BLEManager {
public:
    using DeviceCallback = std::function<void(const BLEDeviceInfo& device)>;
    using DataCallback = std::function<void(const std::vector<uint8_t>& data)>;

    BLEManager();
    ~BLEManager();

    // Scan for BLE devices
    std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds = 5);

    // Connect to a specific device by address or UUID (platform-dependent)
    bool connect(const std::string& device_identifier);

    // Disconnect from current device
    void disconnect();

    // Subscribe to device discovery events
    void onDeviceFound(DeviceCallback callback);

    // Subscribe to raw BLE data
    void onDataReceived(DataCallback callback);

    // Check connection status
    bool isConnected() const;

    // Get currently connected device identifier
    std::string getConnectedDeviceId() const;

    // Set the BLE platform implementation (for testing/configuration)
    void setPlatform(std::unique_ptr<BLEPlatform> platform);

private:
    std::unique_ptr<BLEPlatform> platform_;
    DeviceCallback device_callback_;
    DataCallback data_callback_;
};

} // namespace vehicle_sim
