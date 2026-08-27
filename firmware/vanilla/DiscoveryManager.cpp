#include "DiscoveryManager.h"
#include <cstring>
#include <algorithm>

namespace esp32_firmware {

DiscoveryManager::DiscoveryManager(IUdp& udp, IWiFiDiscovery& wifi, ITime& time,
                                   const std::array<uint8_t, 16>& deviceId,
                                   IDiscoverySigner& signer)
    : udp_(udp), wifi_(wifi), time_(time), deviceId_(deviceId), signer_(signer) {}

void DiscoveryManager::init() {
    udp_.begin(DiscoveryConfig::DISCOVERY_PORT);
    resetBackoff();
}

void DiscoveryManager::update(uint32_t now, bool haveClient) {
    if (!shouldBroadcast(haveClient)) {
        return;
    }

    uint32_t ageMs = now - ctx_.connectTimeMs;
    uint32_t intervalMs = discoveryIntervalMs(ageMs);

    if (now - ctx_.lastBroadcastMs >= intervalMs) {
        ctx_.lastBroadcastMs = now;
        broadcast();
    }
}

void DiscoveryManager::resetBackoff() {
    ++resetCount_;
    ctx_.connectTimeMs = time_.millis();
    ctx_.lastBroadcastMs = 0;
    ctx_.backoffActive = true;
}

bool DiscoveryManager::forceBroadcast() {
    return broadcast();
}

bool DiscoveryManager::shouldBroadcast(bool haveClient) const {
    // Always broadcast — the device must remain discoverable even with a connected client.
    // Previously: suppress entirely when haveClient. That hid the device once any client
    // connected, AND masked a stuck-haveClient state (discovery silently stopped). The
    // time-based backoff in update() already slows the cadence; no full suppress needed.
    (void)haveClient;

    int mode = wifi_.getMode();
    if (mode == 2) {  // WIFI_AP
        return true;
    } else if (mode == 1) {  // WIFI_STA
        // Broadcast during connection/reconnection, not just when fully connected
        // UDP will fail silently if WiFi isn't truly ready
        return true;
    }
    return false;
}

bool DiscoveryManager::broadcast() {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> packet;

    uint64_t timestamp = time_.getCurrentTimestamp();
    uint32_t nonce = time_.millis();

    buildDiscoveryPacket(packet.data(), deviceId_.data(),
                         DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT,
                         timestamp, nonce);

    // Sign discovery packet if signing is enabled
    signer_.signPacket(packet.data());

    std::string broadcastIp = wifi_.broadcastIP();

    // Defect 1 fix: the packet only "counts" as a real broadcast if it
    // actually leaves the radio. beginPacket()/endPacket() return <= 0 on
    // failure (e.g. ESP32 DNS-resolving a literal broadcast address — see
    // ArduinoUdp.h Defect 2). Previously the count was incremented BEFORE
    // send and the return codes were ignored, so a send that never reached
    // the wire was still counted as success (and the callback fired on a
    // phantom packet). That mismatch is exactly what the tcpdump anomaly
    // showed: broadcast() called ~264× but only ~1 packet on the wire.
    if (udp_.beginPacket(broadcastIp, DiscoveryConfig::DISCOVERY_PORT) <= 0) {
        return false;
    }
    if (udp_.write(packet.data(), DiscoveryConfig::DISCOVERY_PACKET_SIZE)
            != DiscoveryConfig::DISCOVERY_PACKET_SIZE) {
        return false;
    }
    if (udp_.endPacket() <= 0) {
        return false;
    }

    // Only now is the broadcast genuinely on the wire.
    ++broadcastCount_;

    // Invoke callback if set (lets the host observe a confirmed send).
    if (broadcastCallback_) {
        broadcastCallback_(packet.data(), packet.size());
    }

    return true;
}

// Testable pure functions

uint32_t DiscoveryManager::discoveryIntervalMs(uint32_t ageMs) {
    if (ageMs < DiscoveryConfig::DISCOVERY_AGE_2_MIN_MS) {
        return DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS;
    } else if (ageMs < DiscoveryConfig::DISCOVERY_AGE_5_MIN_MS) {
        return DiscoveryConfig::DISCOVERY_INTERVAL_2_5_MIN_MS;
    } else if (ageMs < DiscoveryConfig::DISCOVERY_AGE_10_MIN_MS) {
        return DiscoveryConfig::DISCOVERY_INTERVAL_5_10_MIN_MS;
    } else {
        return DiscoveryConfig::DISCOVERY_INTERVAL_SLOW_MS;
    }
}

void DiscoveryManager::buildDiscoveryPacket(uint8_t* packet, const uint8_t* deviceId,
                                            uint32_t tcpPort, uint16_t otaPort,
                                            uint64_t timestamp, uint32_t nonce) {
    // Magic: "VSIM"
    packet[0] = 0x56; packet[1] = 0x53; packet[2] = 0x49; packet[3] = 0x4D;
    // Version
    packet[4] = 1;
    // Packet type: 1 = discovery
    packet[5] = 1;
    // Device ID (16 bytes)
    std::memcpy(packet + 6, deviceId, 16);
    // Nonce (8 bytes) — use millis as simple nonce
    std::memcpy(packet + 22, &nonce, 4);
    std::memset(packet + 26, 0, 4);
    // Timestamp (8 bytes, big-endian Unix epoch)
    for (int i = 7; i >= 0; --i) {
        packet[30 + i] = static_cast<uint8_t>(timestamp & 0xFF);
        timestamp >>= 8;
    }
    // CAN port (2 bytes, big-endian)
    packet[38] = (tcpPort >> 8) & 0xFF;
    packet[39] = tcpPort & 0xFF;
    // OTA port (2 bytes, big-endian)
    packet[40] = (otaPort >> 8) & 0xFF;
    packet[41] = otaPort & 0xFF;

    // Signature bytes remain zeroed for unsigned discovery broadcasts.
    // If signing is enabled, signDiscoveryPacket() will fill this in.
    std::memset(packet + 42, 0, 64);
}

} // namespace esp32_firmware