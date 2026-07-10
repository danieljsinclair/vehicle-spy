#pragma once

// DiscoveryManager.h - Vanilla C++ UDP discovery broadcast
// Extracted from can-bridge.ino for host testability

#include <cstdint>
#include <string>
#include <array>
#include <functional>

namespace esp32_firmware {

// Discovery configuration
struct DiscoveryConfig {
    static constexpr uint16_t DISCOVERY_PORT = 3335;
    static constexpr size_t DISCOVERY_PACKET_SIZE = 106;  // 42 header + 64 signature
    static constexpr uint32_t TCP_PORT = 3333;
    static constexpr uint16_t OTA_HTTP_PORT = 80;

    // Backoff schedule
    static constexpr uint32_t DISCOVERY_INTERVAL_FAST_MS = 500;
    static constexpr uint32_t DISCOVERY_INTERVAL_1_5_MIN_MS = 3000;
    static constexpr uint32_t DISCOVERY_INTERVAL_5_10_MIN_MS = 6000;
    static constexpr uint32_t DISCOVERY_INTERVAL_10_15_MIN_MS = 10000;
    static constexpr uint32_t DISCOVERY_INTERVAL_15_30_MIN_MS = 30000;
    static constexpr uint32_t DISCOVERY_INTERVAL_SLOW_MS = 60000;

    static constexpr uint32_t DISCOVERY_AGE_1_MIN_MS = 60000;
    static constexpr uint32_t DISCOVERY_AGE_5_MIN_MS = 300000;
    static constexpr uint32_t DISCOVERY_AGE_10_MIN_MS = 600000;
    static constexpr uint32_t DISCOVERY_AGE_15_MIN_MS = 900000;
    static constexpr uint32_t DISCOVERY_AGE_30_MIN_MS = 1800000;
};

// Discovery state
struct DiscoveryContext {
    uint32_t connectTimeMs = 0;
    uint32_t lastBroadcastMs = 0;
    bool backoffActive = false;
};

// UDP interface for broadcasting
struct IUdp {
    virtual void begin(uint16_t port) = 0;
    virtual int beginPacket(const std::string& ip, uint16_t port) = 0;
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual int endPacket() = 0;
    virtual ~IUdp() = default;
};

// WiFi interface for getting broadcast IP
struct IWiFiDiscovery {
    virtual int getMode() const = 0;
    virtual int status() const = 0;
    virtual std::string broadcastIP() const = 0;
    virtual ~IWiFiDiscovery() = default;
};

// Time interface for timestamps
struct ITime {
    virtual uint64_t getCurrentTimestamp() const = 0;
    virtual uint32_t millis() const = 0;
    virtual ~ITime() = default;
};

// Discovery signing interface (optional)
struct IDiscoverySigner {
    virtual void signPacket(uint8_t* packet) = 0;
    virtual bool isEnabled() const = 0;
    virtual ~IDiscoverySigner() = default;
};

// No-op signer for when signing is disabled
struct NullDiscoverySigner : public IDiscoverySigner {
    void signPacket(uint8_t* packet) override { (void)packet; }
    bool isEnabled() const override { return false; }
};

class DiscoveryManager {
public:
    using BroadcastCallback = std::function<void(const uint8_t* packet, size_t len)>;

    DiscoveryManager(IUdp& udp, IWiFiDiscovery& wifi, ITime& time,
                     const std::array<uint8_t, 16>& deviceId,
                     IDiscoverySigner& signer = getNullSigner());

    // Initialize discovery (start UDP, reset backoff)
    void init();

    // Main update loop - call from main loop()
    void update(uint32_t now, bool haveClient);

    // Reset discovery backoff timer (call on boot and disconnect)
    void resetBackoff();

    // Force a discovery broadcast (for testing)
    void forceBroadcast();

    // Get current context (for testing)
    const DiscoveryContext& getContext() const { return ctx_; }

    // Set broadcast callback (for testing/inspection)
    void setBroadcastCallback(BroadcastCallback cb) {
        broadcastCallback_ = std::move(cb);
        // DEBUG: Callback has been set
        (void)broadcastCallback_;  // Suppress unused warning
    }

    // Testable pure functions
    static uint32_t discoveryIntervalMs(uint32_t ageMs);
    static void buildDiscoveryPacket(uint8_t* packet, const uint8_t* deviceId,
                                     uint32_t tcpPort, uint16_t otaPort,
                                     uint64_t timestamp, uint32_t nonce);

    // Testable predicate: whether a discovery broadcast should fire given the
    // current WiFi mode and whether a buddy client is already connected.
    bool shouldBroadcast(bool haveClient) const;

private:
    IUdp& udp_;
    IWiFiDiscovery& wifi_;
    ITime& time_;
    const std::array<uint8_t, 16>& deviceId_;
    IDiscoverySigner& signer_;

    DiscoveryContext ctx_;
    BroadcastCallback broadcastCallback_;

    static NullDiscoverySigner& getNullSigner() {
        static NullDiscoverySigner instance;
        return instance;
    }

    void broadcast();
};

} // namespace esp32_firmware