#pragma once

#include "vehicle-sim/ble/BLEPlatform.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"

namespace vehicle_sim {

/**
 * @brief Mock BLE platform for testing and simulator builds.
 *
 * Simulates BLE device discovery and data streaming without real hardware.
 * Useful for development, CI, and iOS simulator builds where CoreBluetooth
 * is unavailable or requires physical device.
 */
class BLEManagerMock : public BLEPlatform {
public:
    BLEManagerMock();
    ~BLEManagerMock() override;

    // BLEPlatform interface
    std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds) override;
    bool connect(std::string_view device_identifier) override;
    void disconnect() override;
    void setDeviceFoundCallback(DeviceCallback callback) override;
    void setDataReceivedCallback(DataCallback callback) override;
    void send(const std::vector<uint8_t>& data) override;
    bool isConnected() const override;
    std::string getConnectedDeviceId() const override;

    // Mock-specific control
    void simulateIncomingData(const std::vector<uint8_t>& data) const;
    void setDeviceList(const std::vector<BLEDeviceInfo>& devices);

private:
    bool connected_{false};
    std::string connected_device_id_;
    DeviceCallback device_callback_;
    DataCallback data_callback_;
    std::vector<BLEDeviceInfo> mock_devices_;
};

} // namespace vehicle_sim
