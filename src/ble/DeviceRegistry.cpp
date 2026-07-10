#include "vehicle-sim/ble/DeviceRegistry.h"

#include <iostream>
#include <iomanip>

namespace vehicle_sim {
namespace {

// Pad a UTF-8 string to a fixed display width (counts code points, not bytes,
// so multi-byte glyphs align). Extracted verbatim from the former
// BLEManagerBase device-listing helper.
std::string padToWidth(std::string_view s, int width) {
    int displayWidth = 0;
    for (char c : s) {
        displayWidth += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    }
    if (displayWidth >= width) return std::string(s);
    return std::string(s) + std::string(width - displayWidth, ' ');
}

} // anonymous namespace

void DeviceRegistry::addDiscoveredDevice(const BLEDeviceInfo& device) {
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

    // Invoke the device-found callback (through the hub).
    callbacks_.invokeDeviceCallback(device);
}

void DeviceRegistry::clearDiscoveredDevices() {
    std::scoped_lock lock(devices_mutex_);
    discovered_devices_.clear();
}

std::optional<BLEDeviceInfo> DeviceRegistry::findDeviceByAddress(std::string_view address) const {
    std::scoped_lock lock(devices_mutex_);

    for (const auto& device : discovered_devices_) {
        if (device.address == address) {
            return device;
        }
    }

    return std::nullopt;
}

} // namespace vehicle_sim
