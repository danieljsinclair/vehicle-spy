// DiscoveryPolicy.h - UDP discovery broadcast policy
// Extracted from FirmwareApp for S1448 method-count compliance.
//
// Owns the DiscoveryManager and its lifecycle: init-on-first-tick, per-tick
// update, backoff reset on WiFi reconnect, and enable/disable toggle.
// FirmwareApp delegates discovery_ state and calls update() from its own
// update() loop.

#pragma once

#include <cstdint>
#include <functional>
#include "DiscoveryManager.h"

namespace esp32_firmware {

struct IWiFiDiscovery;
struct ITime;
struct IUdp;

// DiscoveryPolicy: owns DiscoveryManager + discovery lifecycle.
// Separated from FirmwareApp for S1448 compliance.
class DiscoveryPolicy {
public:
    using BroadcastCallback = std::function<void()>;

    DiscoveryPolicy(IUdp& udp, IWiFiDiscovery& wifiDiscovery, ITime& time,
                    const std::array<uint8_t, 16>& deviceId,
                    std::function<void()> broadcastCallback,
                    std::function<void()> backoffResetCallback);

    // Lazily init the UDP socket on first call (deferred out of boot path).
    void startIfNeeded();

    // Advance discovery state machine. Pass live TCP-client state.
    void update(uint32_t now, bool clientConnected);

    // Reset backoff timer (called on WiFi reconnect).
    void resetBackoff();

    // Enable/disable discovery broadcasts.
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // Current broadcast cadence as human string ("500ms", "10s", or "none").
    std::string cadenceString(uint32_t nowMs) const;

    // For observability: number of broadcasts sent.
    const DiscoveryContext& context() const { return manager_ ? manager_->getContext() : emptyCtx_; }
    uint32_t broadcastCount() const;

private:
    DiscoveryContext emptyCtx_;

private:
    IUdp& udp_;
    IWiFiDiscovery& wifiDiscovery_;
    ITime& time_;
    std::array<uint8_t, 16> deviceId_;
    std::function<void()> broadcastCallback_;
    std::function<void()> backoffResetCallback_;
    std::unique_ptr<DiscoveryManager> manager_;
    bool started_ = false;
    bool enabled_ = true;
};

} // namespace esp32_firmware
