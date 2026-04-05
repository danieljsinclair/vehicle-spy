#pragma once

#include "vehicle-sim/ble/BLEPlatform.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"

// Forward declarations for CoreBluetooth (framework available on iOS/macOS)
#ifdef __APPLE__
    #import <CoreBluetooth/CoreBluetooth.h>
#endif

namespace vehicle_sim {

/**
 * @brief iOS BLE platform implementation using CoreBluetooth.
 *
 * Uses Apple's CoreBluetooth framework to scan, connect, and communicate
 * with BLE peripherals (Tesla OBD2 adapter). Requires iOS 15+ or macOS 12+.
 */
class BLEManageriOS : public BLEPlatform {
public:
    BLEManageriOS();
    ~BLEManageriOS() override;

    // BLEPlatform interface
    std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds) override;
    bool connect(const std::string& device_identifier) override;
    void disconnect() override;
    void setDeviceFoundCallback(DeviceCallback callback) override;
    void setDataReceivedCallback(DataCallback callback) override;
    bool isConnected() const override;
    std::string getConnectedDeviceId() const override;

private:
#ifdef __APPLE__
    // CoreBluetooth central manager
    CBCentralManager* central_manager_;
    CBPeripheral* connected_peripheral_;
#endif

    bool connected_;
    std::string connected_device_id_;
    DeviceCallback device_callback_;
    DataCallback data_callback_;

    // Platform-specific callbacks from CoreBluetooth
    // These will be called from Objective-C++ delegate
    void onDeviceDiscovered(const BLEDeviceInfo& device);
    void onDataReceived(const std::vector<uint8_t>& data);
    void onConnectionStateChanged(bool is_connected);
};

} // namespace vehicle_sim
