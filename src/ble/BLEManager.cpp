#include "vehicle-sim/BLEManager.h"
#if TARGET_OS_OSX
#include "vehicle-sim/ble/platform/BLEManagerMacOS.h"
#elif TARGET_OS_IPHONE
#include "vehicle-sim/ble/platform/BLEManageriOS.h"
#endif
#include <iostream>

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_OSX
        #define PLATFORM_MACOS 1
    #elif TARGET_OS_IPHONE
        #define PLATFORM_IOS 1
    #endif
#endif

namespace vehicle_sim {

BLEManager::BLEManager()
    : platform_(createDefaultPlatform()) {
}

BLEManager::~BLEManager() = default;

std::unique_ptr<BLEManagerBase> BLEManager::createDefaultPlatform() {
#if defined(PLATFORM_MACOS)
    std::cout << "[BLEManager] Creating macOS BLE platform" << std::endl;
    return std::make_unique<BLEManagerMacOS>();
#elif defined(PLATFORM_IOS)
    std::cout << "[BLEManager] Creating iOS BLE platform" << std::endl;
    return std::make_unique<BLEManageriOS>();
#else
    // Default to iOS for Apple platforms (most common use case)
    // macOS CLI will use BLEManagerMacOS when built on macOS
    std::cout << "[BLEManager] Creating default BLE platform (iOS)" << std::endl;
    return std::make_unique<BLEManageriOS>();
#endif
}

std::vector<BLEDeviceInfo> BLEManager::scanForDevices(int timeout_seconds) {
    if (platform_) {
        auto devices = platform_->scanForDevices(timeout_seconds);
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
    device_callback_ = std::move(callback);
    if (platform_) {
        platform_->setDeviceFoundCallback([this](const BLEDeviceInfo& device) {
            // Forward to the user's callback
            if (device_callback_) {
                device_callback_(device);
            }
        });
    }
}

void BLEManager::onDataReceived(DataCallback callback) {
    data_callback_ = std::move(callback);
    if (platform_) {
        platform_->setDataReceivedCallback([this](const std::vector<uint8_t>& data) {
            // Forward to the user's callback
            if (data_callback_) {
                data_callback_(data);
            }
        });
    }
}

bool BLEManager::isConnected() const {
    return platform_ ? platform_->isConnected() : false;
}

std::string BLEManager::getConnectedDeviceId() const {
    return platform_ ? platform_->getConnectedDeviceId() : std::string();
}

void BLEManager::setPlatform(std::unique_ptr<BLEManagerBase> platform) {
    platform_ = std::move(platform);
}

BLEManagerBase* BLEManager::getPlatform() const {
    return platform_.get();
}

bool BLEManager::initializeELM327() {
    return platform_ ? platform_->initializeELM327() : false;
}

void BLEManager::startOBD2Polling(int interval_ms) {
    if (platform_) {
        platform_->startOBD2Polling(interval_ms);
    }
}

void BLEManager::stopOBD2Polling() {
    if (platform_) {
        platform_->stopOBD2Polling();
    }
}

} // namespace vehicle_sim