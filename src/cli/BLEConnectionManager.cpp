#include "vehicle-sim/cli/BLEConnectionManager.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include <iostream>
#include <iomanip>

namespace vehicle_sim::cli {

constexpr int CHARACTERISTICS_WAIT_MS = 10000;
constexpr int MIN_RAW_DATA_LOG_LENGTH = 3;

BLEConnectionManager::BLEConnectionManager(std::unique_ptr<BLEManager> bleManager) noexcept
    : bleManager_(std::move(bleManager))
    , isConnected_(false)
    , polling_(false)
    , protocol_(domain::VehicleProtocol::OBD2)
{
}

BLEConnectionManager::~BLEConnectionManager() {
    if (polling_) {
        stopPolling();
    }
    if (isConnected_) {
        disconnect();
    }
}

bool BLEConnectionManager::connect(const std::string& address,
                                    domain::VehicleProtocol protocol,
                                    DataCallback callback) {
    protocol_ = protocol;
    const char* protocolLabel = (protocol == domain::VehicleProtocol::CAN) ? "CAN" : "OBD2";

    std::cout << "Attempting to connect to: " << address << "\n";

    if (!bleManager_->connect(address)) {
        std::cerr << "\nFailed to connect to BLE device: " << address << "\n\n"
                  << "Possible reasons:\n"
                  << "  - Device address is incorrect\n"
                  << "  - Device is out of range\n"
                  << "  - Device is already connected to another application\n"
                  << "  - OBD2 adapter lost power\n"
                  << "\nTry running --scan to verify device is available.\n";
        return false;
    }

    std::cout << "Connected! Waiting for service/characteristic discovery...\n";

    if (!bleManager_->waitForCharacteristics(CHARACTERISTICS_WAIT_MS)) {
        std::cerr << "\nFailed to discover required BLE characteristics.\n"
                  << "Check that the OBD2 adapter is powered and in range.\n";
        disconnect();
        return false;
    }
    std::cout << "Characteristics ready.\n";

    if (protocol == domain::VehicleProtocol::CAN) {
        std::cout << "Initializing CAN monitor mode...\n";
        if (!bleManager_->initializeCANMonitor()) {
            std::cerr << "Failed to initialize ELM327 for CAN monitoring\n";
            disconnect();
            return false;
        }
    } else {
        std::cout << "Initializing OBD2 adapter...\n";
        if (!bleManager_->initializeELM327()) {
            std::cerr << "Failed to initialize ELM327 adapter\n";
            disconnect();
            return false;
        }
    }

    std::cout << "Starting " << protocolLabel << " data polling...\n";
    std::cout << "Press Ctrl+C to stop\n";

    isConnected_ = true;
    bleManager_->onDataReceived([this, callback, protocolLabel](const std::vector<std::uint8_t>& data) {
        if (data.size() >= MIN_RAW_DATA_LOG_LENGTH) {
            std::cout << "[" << protocolLabel << "] Raw response: ";
            for (auto b : data) {
                std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)b << " ";
            }
            std::cout << std::dec << std::endl;
        }
        callback(data);
    });

    return true;
}

void BLEConnectionManager::startPolling(int updateIntervalMs) {
    if (!isConnected_) return;

    if (protocol_ == domain::VehicleProtocol::CAN) {
        bleManager_->startCANMonitor(updateIntervalMs);
    } else {
        bleManager_->startOBD2Polling(updateIntervalMs);
    }
    polling_ = true;
}

void BLEConnectionManager::stopPolling() {
    if (!polling_) return;

    if (protocol_ == domain::VehicleProtocol::CAN) {
        bleManager_->stopCANMonitor();
    } else {
        bleManager_->stopOBD2Polling();
    }
    polling_ = false;
}

void BLEConnectionManager::disconnect() {
    if (!isConnected_) return;

    std::cout << "Disconnecting...\n";
    bleManager_->disconnect();
    isConnected_ = false;
}

bool BLEConnectionManager::isConnected() const {
    return bleManager_->isConnected();
}

int BLEConnectionManager::notificationCount() const {
    return bleManager_->bleNotificationCount();
}

std::string BLEConnectionManager::lastRawHex() const {
    return bleManager_->lastRawHex();
}

const domain::VehicleDetector* BLEConnectionManager::vehicleDetector() const {
    return bleManager_->vehicleDetector();
}

bool BLEConnectionManager::connectionLost() const {
    return isConnected_ && !bleManager_->isConnected();
}

} // namespace vehicle_sim::cli