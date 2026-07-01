#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/domain/VehicleDetector.h"
#include <iostream>

#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#include "vehicle-sim/ble/platform/BLEManagerMacOS.h"
#elif defined(__APPLE__) && TARGET_OS_IPHONE
#include "vehicle-sim/ble/platform/BLEManageriOS.h"
#endif

namespace vehicle_sim {

BLEManager::BLEManager() = default;

BLEManager::~BLEManager() = default;

std::unique_ptr<BLEManagerBase> BLEManager::createDefaultPlatform() {
#if defined(__APPLE__) && TARGET_OS_OSX
    std::cout << "[BLEManager] Creating macOS BLE platform" << std::endl;
    return std::make_unique<BLEManagerMacOS>();
#elif defined(__APPLE__) && TARGET_OS_IPHONE
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

bool BLEManager::waitForCharacteristics(int timeout_ms) {
    return platform_ ? platform_->waitForCharacteristics(timeout_ms) : false;
}

bool BLEManager::initializeELM327() {
    return platform_ ? platform_->elm327Session().initializeELM327() : false;
}

void BLEManager::startOBD2Polling(int interval_ms) {
    if (platform_) {
        platform_->elm327Session().startOBD2Polling(interval_ms);
    }
}

void BLEManager::stopOBD2Polling() {
    if (platform_) {
        platform_->elm327Session().stopOBD2Polling();
    }
}

bool BLEManager::initializeCANMonitor() {
    if (platform_) return platform_->elm327Session().initializeCANMonitor();
    return false;
}

bool BLEManager::initializeForVINQuery() {
    if (platform_) return platform_->elm327Session().initializeForVINQuery();
    return false;
}

std::optional<std::string> BLEManager::queryVIN(int timeout_ms) {
    if (platform_) return platform_->elm327Session().queryVIN(timeout_ms);
    return std::nullopt;
}

void BLEManager::startCANMonitor(int interval_ms) {
    if (platform_) platform_->elm327Session().startCANMonitor(interval_ms);
}

void BLEManager::stopCANMonitor() {
    if (platform_) platform_->elm327Session().stopCANMonitor();
}

std::optional<domain::VehicleDetectionResult> BLEManager::initializeOBD2WithDetection() {
    return platform_ ? platform_->elm327Session().initializeOBD2WithDetection() : std::nullopt;
}

void BLEManager::processOBD2Data(const std::string& asciiData) {
    if (platform_) {
        platform_->elm327Session().processOBD2Data(asciiData);
    }
}

domain::VehicleDetector* BLEManager::vehicleDetector() {
    return platform_ ? platform_->vehicleDetector() : nullptr;
}

int BLEManager::bleNotificationCount() const {
    return platform_ ? platform_->bleNotificationCount() : 0;
}

std::string BLEManager::lastRawHex() const {
    return platform_ ? platform_->lastRawHex() : "";
}

} // namespace vehicle_sim