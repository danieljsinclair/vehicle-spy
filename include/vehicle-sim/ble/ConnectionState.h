#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "vehicle-sim/ble/CallbackHub.h"

namespace vehicle_sim {

/**
 * @brief Collaborator holding the connection state (connected flag + connected
 *        device id) and firing the connection callback on change. Extracted
 *        from BLEManagerBase (cpp:S1448).
 *
 * Construction is deferred to BLEManagerBase::InitConnectionState() so the hub
 * reference is available; the base constructs it via the protected init method.
 */
class ConnectionState {
public:
    explicit ConnectionState(CallbackHub& callbacks) : callbacks_(callbacks) {}

    /// Set the raw connected flag (no callback).
    void setConnected(bool c) noexcept { connected_ = c; }

    /// Read the raw connected flag.
    [[nodiscard]] bool isConnectedRaw() const noexcept { return connected_; }

    /// Set the connected device id.
    void setConnectedDeviceId(std::string id) { connected_device_id_ = std::move(id); }

    /// Read the connected device id.
    [[nodiscard]] const std::string& connectedDeviceIdRaw() const noexcept { return connected_device_id_; }

    /// Update connection state and fire the connection callback.
    void setConnectionState(bool connected, std::string_view device_id = "") {
        connected_ = connected;
        connected_device_id_ = device_id;
        callbacks_.invokeConnectionCallback(connected, device_id);
    }

private:
    std::atomic<bool> connected_{false};
    std::string connected_device_id_;
    CallbackHub& callbacks_;
};

} // namespace vehicle_sim
