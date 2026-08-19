#include <array>
// DiscoveryPolicy.cpp - Discovery policy implementation

#include "DiscoveryPolicy.h"
#include "DiscoveryManager.h"

namespace esp32_firmware {

DiscoveryPolicy::DiscoveryPolicy(IUdp& udp, IWiFiDiscovery& wifiDiscovery, ITime& time,
                                 const std::array<uint8_t, 16>& deviceId,
                                 std::function<void()> broadcastCallback,
                                 std::function<void()> backoffResetCallback)
    : udp_(udp)
    , wifiDiscovery_(wifiDiscovery)
    , time_(time)
    , deviceId_(deviceId)
    , broadcastCallback_(std::move(broadcastCallback))
    , backoffResetCallback_(std::move(backoffResetCallback)) {
    // Own the DiscoveryManager. The broadcast callback adapts the (packet, len)
    // signature from DiscoveryManager into the parameterless firmware effect.
    manager_ = std::make_unique<DiscoveryManager>(udp_, wifiDiscovery_, time_, deviceId_);
    manager_->setBroadcastCallback([this](const uint8_t* packet, size_t len) {
        (void)packet;
        (void)len;
        if (broadcastCallback_) broadcastCallback_();
    });
}

void DiscoveryPolicy::startIfNeeded() {
    if (!started_ && manager_ && enabled_) {
        manager_->init();
        started_ = true;
    }
}

void DiscoveryPolicy::update(uint32_t now, bool clientConnected) {
    if (manager_ && enabled_) {
        manager_->update(now, clientConnected);
    }
}

void DiscoveryPolicy::resetBackoff() {
    if (manager_) {
        manager_->resetBackoff();
    }
}

std::string DiscoveryPolicy::cadenceString(uint32_t nowMs) const {
    if (!manager_ || !enabled_ || !started_) {
        return "none";
    }
    const DiscoveryContext& ctx = manager_->getContext();
    uint32_t ageMs = (nowMs > ctx.connectTimeMs) ? (nowMs - ctx.connectTimeMs) : 0;
    uint32_t intervalMs = DiscoveryManager::discoveryIntervalMs(ageMs);
    if (intervalMs >= 1000) {
        return std::to_string(intervalMs / 1000) + "s";
    }
    return std::to_string(intervalMs) + "ms";
}

uint32_t DiscoveryPolicy::broadcastCount() const {
    return manager_ ? manager_->broadcastCount() : 0;
}

} // namespace esp32_firmware
