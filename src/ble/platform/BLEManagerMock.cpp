#include "vehicle-sim/ble/platform/BLEManagerMock.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"

namespace vehicle_sim {

BLEManagerMock::BLEManagerMock()
    : connected_(false) {
}

BLEManagerMock::~BLEManagerMock() {
}

std::vector<BLEDeviceInfo> BLEManagerMock::scanForDevices(int /*timeout_seconds*/) {
    // If mock devices not set, return default Tesla mock
    if (mock_devices_.empty()) {
        BLEDeviceInfo tesla;
        tesla.address = "00:11:22:33:44:55";
        tesla.name = "Tesla Model Y OBD2";
        tesla.isConnected = false;
        mock_devices_.push_back(tesla);
    }

    // Invoke callback for each device found (simulating async scan)
    for (const auto& device : mock_devices_) {
        if (device_callback_) {
            device_callback_(device);
        }
    }

    return mock_devices_;
}

bool BLEManagerMock::connect(std::string_view device_identifier) {
    connected_ = true;
    connected_device_id_ = std::string(device_identifier);
    return true;
}

void BLEManagerMock::disconnect() {
    connected_ = false;
    connected_device_id_.clear();
}

void BLEManagerMock::setDeviceFoundCallback(DeviceCallback callback) {
    device_callback_ = std::move(callback);
}

void BLEManagerMock::setDataReceivedCallback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

void BLEManagerMock::send(const std::vector<uint8_t>& /*data*/) {
    // Mock implementation - in real implementation, this would write to BLE characteristic
    // For testing, we can optionally echo back data or track sent data
}

bool BLEManagerMock::isConnected() const {
    return connected_;
}

std::string BLEManagerMock::getConnectedDeviceId() const {
    return connected_device_id_;
}

void BLEManagerMock::simulateIncomingData(const std::vector<uint8_t>& data) {
    if (data_callback_ && connected_) {
        data_callback_(data);
    }
}

void BLEManagerMock::setDeviceList(const std::vector<BLEDeviceInfo>& devices) {
    mock_devices_ = devices;
}

} // namespace vehicle_sim