#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/ble/platform/BLEManagerMock.h"
#include <iostream>

namespace vehicle_sim {

BLEManager::BLEManager()
    : platform_(std::make_unique<BLEManagerMock>()) {
}

BLEManager::~BLEManager() = default;

std::vector<BLEDeviceInfo> BLEManager::scanForDevices(int timeout_seconds) {
    if (platform_) {
        // Wire callbacks to platform
        if (platform_->isConnected()) {
            // Already connected, maybe disconnect first or handle differently
        }

        auto devices = platform_->scanForDevices(timeout_seconds);

        // Invoke callbacks for discovered devices (already done by platform)
        return devices;
    }
    return {};
}

bool BLEManager::connect(const std::string& device_identifier) {
    if (!platform_) return false;

    bool success = platform_->connect(device_identifier);
    if (success) {
        std::cout << "[BLEManager] Connected to device: " << device_identifier << std::endl;
    } else {
        std::cout << "[BLEManager] Connection failed" << std::endl;
    }
    return success;
}

void BLEManager::disconnect() {
    if (platform_) {
        platform_->disconnect();
        std::cout << "[BLEManager] Disconnected" << std::endl;
    }
}

void BLEManager::onDeviceFound(DeviceCallback callback) {
    // Set callback on underlying platform
    if (platform_) {
        platform_->setDeviceFoundCallback(std::move(callback));
    }
}

void BLEManager::onDataReceived(DataCallback callback) {
    // Set callback on underlying platform
    if (platform_) {
        platform_->setDataReceivedCallback(std::move(callback));
    }
}

bool BLEManager::isConnected() const {
    return platform_ ? platform_->isConnected() : false;
}

std::string BLEManager::getConnectedDeviceId() const {
    return platform_ ? platform_->getConnectedDeviceId() : std::string();
}

void BLEManager::setPlatform(std::unique_ptr<BLEPlatform> platform) {
    if (platform) {
        platform_ = std::move(platform);
    }
}

} // namespace vehicle_sim
