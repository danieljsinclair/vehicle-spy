#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vehicle-sim/ble/BLEDeviceInfo.h"
#include "vehicle-sim/ble/CallbackHub.h"

namespace vehicle_sim {

/**
 * @brief Collaborator holding the discovered-device cache and its mutex.
 *        Extracted from BLEManagerBase (cpp:S1448).
 *
 * On add, the device-found callback is fired through the supplied CallbackHub
 * so the consumer is notified exactly as before. Lookups and clears are
 * mutex-guarded; the reference-returning accessors let callers copy the vector
 * out (e.g. the platform scan path) without an extra copy.
 */
class DeviceRegistry {
public:
    explicit DeviceRegistry(CallbackHub& callbacks) : callbacks_(callbacks) {}

    /// Add a discovered device, deduplicating by address. Fires the
    /// device-found callback for genuinely-new devices. Thread-safe.
    void addDiscoveredDevice(const BLEDeviceInfo& device);

    /// Clear the discovered-device cache. Thread-safe.
    void clearDiscoveredDevices();

    /// Find a discovered device by address. Thread-safe.
    [[nodiscard]] std::optional<BLEDeviceInfo> findDeviceByAddress(std::string_view address) const;

    /// Reference to the discovered-device vector (insertion-ordered).
    [[nodiscard]] std::vector<BLEDeviceInfo>& devices() noexcept { return discovered_devices_; }
    [[nodiscard]] const std::vector<BLEDeviceInfo>& devices() const noexcept { return discovered_devices_; }

private:
    mutable std::mutex devices_mutex_;   // guards discovered_devices_
    std::vector<BLEDeviceInfo> discovered_devices_;
    CallbackHub& callbacks_;
};

} // namespace vehicle_sim
