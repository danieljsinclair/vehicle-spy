#include "vehicle-sim/ble/BLEManagerBase.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <string_view>

namespace vehicle_sim {
namespace {

std::string padToWidth(std::string_view s, int width) {
    int displayWidth = 0;
    for (unsigned char c : s) {
        displayWidth += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    if (displayWidth >= width) return std::string(s);
    return std::string(s) + std::string(width - displayWidth, ' ');
}

} // anonymous namespace

// ================================================
// OBD2 UUIDs - Already defined in header as constexpr
// ================================================

// ================================================
// OBD2 PIDs - Already defined in header as constexpr
// ================================================

// ================================================
// BLEManagerBase Implementation
//
// The ELM327/OBD2 protocol, polling, CAN-monitor, VIN and auto-detection
// behaviour now lives in the composed Elm327Session (roles 4+5). This file
// holds the BLE transport core: device discovery cache, connection state,
// callbacks, raw-notification bookkeeping, and the thin forwards that expose
// the session's public OBD2 API.
// ================================================

BLEManagerBase::BLEManagerBase(util::IClock* clock) : session_(*this, clock) {}

void BLEManagerBase::setDeviceFoundCallback(DeviceCallback callback) {
    device_callback_ = std::move(callback);
}

void BLEManagerBase::setDataReceivedCallback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

void BLEManagerBase::setConnectionCallback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
}

// ================================================
// BLE Transport Helpers
// ================================================

void BLEManagerBase::sendASCII(std::string_view command) {
    std::vector<uint8_t> bytes(command.begin(), command.end());
    send(bytes);
}

std::string BLEManagerBase::signalQuality(int rssi) {
    if (rssi >= RSSI_EXCELLENT) return "Excellent";
    if (rssi >= RSSI_GOOD) return "Good";
    if (rssi >= RSSI_FAIR) return "Fair";
    return "Poor";
}

// ================================================
// Device Management (Common Implementation)
// ================================================

void BLEManagerBase::addDiscoveredDevice(const BLEDeviceInfo& device) {
    std::scoped_lock lock(devices_mutex_);

    // Check for duplicates
    for (const auto& existing : discovered_devices_) {
        if (existing.address == device.address) {
            return;  // Already have this device
        }
    }

    discovered_devices_.push_back(device);
    std::cout << "  [" << std::setw(2) << discovered_devices_.size() << "] "
              << padToWidth(device.name, 36)
              << padToWidth(device.address, 40)
              << "RSSI:" << std::right << std::setw(4) << device.rssi
              << "  " << (device.isConnected ? "[Connected]" : "[Available]") << std::endl;

    // Invoke callback
    invokeDeviceCallback(device);
}

void BLEManagerBase::clearDiscoveredDevices() {
    std::scoped_lock lock(devices_mutex_);
    discovered_devices_.clear();
}

std::optional<BLEDeviceInfo> BLEManagerBase::findDeviceByAddress(std::string_view address) const {
    std::scoped_lock lock(devices_mutex_);

    for (const auto& device : discovered_devices_) {
        if (device.address == address) {
            return device;
        }
    }

    return std::nullopt;
}

// ================================================
// Callback Invocation
// ================================================

void BLEManagerBase::invokeDeviceCallback(const BLEDeviceInfo& device) const {
    if (device_callback_) {
        device_callback_(device);
    }
}

void BLEManagerBase::invokeDataCallback(const std::vector<uint8_t>& data) {
    // Always track raw BLE activity (before any parsing/dropping).
    ble_notification_count_++;
    {
        std::scoped_lock lock(raw_mutex_);
        std::ostringstream hex;
        for (size_t i = 0; i < std::min(data.size(), size_t(16)); ++i) {
            hex << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]) << " ";
        }
        if (data.size() > 16) hex << "...";
        last_raw_hex_ = hex.str();
    }

    // Route the bytes into the composed ELM327 session: it performs prompt
    // detection (OBD2 mode), CAN/OBD2 parsing, vehicle-detector observation,
    // and delivers any parsed binary to the consumer via sessionDeliverParsed.
    session_.handleIncomingData(data);
}

void BLEManagerBase::invokeConnectionCallback(bool connected, std::string_view device_id) const {
    if (connection_callback_) {
        connection_callback_(connected, std::string(device_id));
    }
}

void BLEManagerBase::setConnectionState(bool connected, std::string_view device_id) {
    connected_ = connected;
    connected_device_id_ = device_id;
    invokeConnectionCallback(connected, device_id);
}

} // namespace vehicle_sim
