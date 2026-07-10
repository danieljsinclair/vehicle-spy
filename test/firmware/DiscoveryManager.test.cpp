#include <gtest/gtest.h>
#include "firmware/vanilla/DiscoveryManager.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace esp32_firmware;

namespace {

// ── Fakes for the four discovery boundaries ───────────────────────────────────
class FakeUdp : public IUdp {
public:
    std::string lastIp;
    uint16_t lastPort = 0;
    std::vector<uint8_t> lastWritten;
    bool beginCalled = false;
    int beginPacketCalls = 0;
    int endPacketCalls = 0;

    void begin(uint16_t port) override { beginCalled = true; (void)port; }
    int beginPacket(const std::string& ip, uint16_t port) override {
        ++beginPacketCalls; lastIp = ip; lastPort = port; return 1;
    }
    size_t write(const uint8_t* data, size_t len) override {
        lastWritten.assign(data, data + len); return len;
    }
    int endPacket() override { ++endPacketCalls; return 1; }
};

class FakeWiFiDiscovery : public IWiFiDiscovery {
public:
    int mode = 1;  // WIFI_STA by default
    int statusVal = 0;
    std::string broadcastIp = "192.168.1.255";
    int getMode() const override { return mode; }
    int status() const override { return statusVal; }
    std::string broadcastIP() const override { return broadcastIp; }
};

class FakeTime : public ITime {
public:
    uint64_t ts = 1700000000;  // fixed epoch
    uint32_t m = 12345;
    uint64_t getCurrentTimestamp() const override { return ts; }
    uint32_t millis() const override { return m; }
};

// Signer that records whether it was invoked and (optionally) writes a marker.
class FakeSigner : public IDiscoverySigner {
public:
    bool enabled = false;
    int signCalls = 0;
    bool isEnabled() const override { return enabled; }
    void signPacket(uint8_t* packet) override {
        ++signCalls;
        if (enabled) {
            // Write a non-zero marker so tests can detect signing happened.
            packet[42] = 0xAA;
        }
    }
};

constexpr std::array<uint8_t, 16> kDeviceId = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

// ── buildDiscoveryPacket wire-format tests (the host-visible contract) ─────────

TEST(DiscoveryPacketTest, MagicAndVersionAndType) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> pkt{};
    DiscoveryManager::buildDiscoveryPacket(pkt.data(), kDeviceId.data(),
        DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT, 42, 7);

    EXPECT_EQ(pkt[0], 0x56);  // 'V'
    EXPECT_EQ(pkt[1], 0x53);  // 'S'
    EXPECT_EQ(pkt[2], 0x49);  // 'I'
    EXPECT_EQ(pkt[3], 0x4D);  // 'M'
    EXPECT_EQ(pkt[4], 1);     // version
    EXPECT_EQ(pkt[5], 1);     // packet type = discovery
}

TEST(DiscoveryPacketTest, DeviceIdCopiedAtOffset6) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> pkt{};
    DiscoveryManager::buildDiscoveryPacket(pkt.data(), kDeviceId.data(),
        DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT, 42, 7);

    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(pkt[6 + i], kDeviceId[i]) << "deviceId byte " << i;
    }
}

TEST(DiscoveryPacketTest, NonceWrittenLittleEndianAt22) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> pkt{};
    const uint32_t nonce = 0x0A0B0C0D;
    DiscoveryManager::buildDiscoveryPacket(pkt.data(), kDeviceId.data(),
        DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT, 42, nonce);

    // memcpy of a uint32_t is host-endian; on this little-endian box expect LE.
    EXPECT_EQ(pkt[22], 0x0D);
    EXPECT_EQ(pkt[23], 0x0C);
    EXPECT_EQ(pkt[24], 0x0B);
    EXPECT_EQ(pkt[25], 0x0A);
}

TEST(DiscoveryPacketTest, TimestampWrittenBigEndianAt30) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> pkt{};
    const uint64_t ts = 0x0102030405060708ULL;
    DiscoveryManager::buildDiscoveryPacket(pkt.data(), kDeviceId.data(),
        DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT, ts, 7);

    EXPECT_EQ(pkt[30], 0x01);  // most significant byte first
    EXPECT_EQ(pkt[37], 0x08);  // least significant byte last
}

TEST(DiscoveryPacketTest, PortsWrittenBigEndian) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> pkt{};
    DiscoveryManager::buildDiscoveryPacket(pkt.data(), kDeviceId.data(),
        0x1234, 0x0050, 42, 7);

    EXPECT_EQ(pkt[38], 0x12); EXPECT_EQ(pkt[39], 0x34);  // TCP port
    EXPECT_EQ(pkt[40], 0x00); EXPECT_EQ(pkt[41], 0x50);  // OTA port
}

TEST(DiscoveryPacketTest, SignatureFieldZeroedWhenUnsigned) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> pkt{};
    std::memset(pkt.data(), 0xFF, pkt.size());  // pre-fill with non-zero
    DiscoveryManager::buildDiscoveryPacket(pkt.data(), kDeviceId.data(),
        DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT, 42, 7);

    for (size_t i = 42; i < DiscoveryConfig::DISCOVERY_PACKET_SIZE; ++i) {
        EXPECT_EQ(pkt[i], 0) << "signature byte " << i << " must be zeroed";
    }
}

// ── discoveryIntervalMs backoff schedule ───────────────────────────────────────

TEST(DiscoveryIntervalTest, FastWithinFirstMinute) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(0),
              DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(59999),
              DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS);
}

TEST(DiscoveryIntervalTest, OneToFiveMinutes) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(60000),
              DiscoveryConfig::DISCOVERY_INTERVAL_1_5_MIN_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(299999),
              DiscoveryConfig::DISCOVERY_INTERVAL_1_5_MIN_MS);
}

TEST(DiscoveryIntervalTest, FiveToTenMinutes) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(300000),
              DiscoveryConfig::DISCOVERY_INTERVAL_5_10_MIN_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(599999),
              DiscoveryConfig::DISCOVERY_INTERVAL_5_10_MIN_MS);
}

TEST(DiscoveryIntervalTest, TenToFifteenMinutes) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(600000),
              DiscoveryConfig::DISCOVERY_INTERVAL_10_15_MIN_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(899999),
              DiscoveryConfig::DISCOVERY_INTERVAL_10_15_MIN_MS);
}

TEST(DiscoveryIntervalTest, FifteenToThirtyMinutes) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(900000),
              DiscoveryConfig::DISCOVERY_INTERVAL_15_30_MIN_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(1799999),
              DiscoveryConfig::DISCOVERY_INTERVAL_15_30_MIN_MS);
}

TEST(DiscoveryIntervalTest, BeyondThirtyMinutesSlow) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(1800000),
              DiscoveryConfig::DISCOVERY_INTERVAL_SLOW_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(999999999),
              DiscoveryConfig::DISCOVERY_INTERVAL_SLOW_MS);
}

// ── shouldBroadcast mode logic ─────────────────────────────────────────────────

TEST(DiscoveryShouldBroadcastTest, NoBroadcastWhenClientConnected) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    EXPECT_FALSE(dm.shouldBroadcast(true));
}

TEST(DiscoveryShouldBroadcastTest, BroadcastInApMode) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    wifi.mode = 2;  // WIFI_AP
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    EXPECT_TRUE(dm.shouldBroadcast(false));
}

TEST(DiscoveryShouldBroadcastTest, BroadcastInStaMode) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    wifi.mode = 1;  // WIFI_STA
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    EXPECT_TRUE(dm.shouldBroadcast(false));
}

TEST(DiscoveryShouldBroadcastTest, NoBroadcastWhenModeUnset) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    wifi.mode = 0;  // mode not set
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    EXPECT_FALSE(dm.shouldBroadcast(false));
}

// ── Update loop + broadcast side effects ───────────────────────────────────────

TEST(DiscoveryUpdateTest, BroadcastRespectsInterval) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    wifi.mode = 1;
    time.m = 1000;  // resetBackoff seeds connectTimeMs here
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    dm.init();  // seeds ctx_, lastBroadcastMs = 0

    // First update at now=1000: age=0, fast interval=500; 1000-0 >= 500 -> broadcast.
    dm.update(1000, false);
    EXPECT_EQ(udp.endPacketCalls, 1);

    // Immediately again at now=1100: 1100-1000=100 < 500 -> no new broadcast.
    dm.update(1100, false);
    EXPECT_EQ(udp.endPacketCalls, 1);

    // At now=1600: 1600-1000=600 >= 500 -> broadcast again.
    dm.update(1600, false);
    EXPECT_EQ(udp.endPacketCalls, 2);
}

TEST(DiscoveryUpdateTest, NoBroadcastWhileClientConnected) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    wifi.mode = 1;
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    dm.init();
    dm.update(10000, true);  // haveClient = true
    EXPECT_EQ(udp.endPacketCalls, 0);
}

TEST(DiscoveryUpdateTest, ForceBroadcastInvokesUdpPipeline) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    dm.init();

    udp.lastWritten.clear();
    dm.forceBroadcast();
    EXPECT_EQ(udp.beginPacketCalls, 1);
    EXPECT_EQ(udp.endPacketCalls, 1);
    ASSERT_EQ(udp.lastWritten.size(), DiscoveryConfig::DISCOVERY_PACKET_SIZE);
    // Broadcast packet must carry the magic header.
    EXPECT_EQ(udp.lastWritten[0], 0x56);
}

TEST(DiscoveryUpdateTest, SignerInvokedDuringBroadcast) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    signer.enabled = true;
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    dm.init();
    dm.forceBroadcast();
    EXPECT_EQ(signer.signCalls, 1);
}

TEST(DiscoveryUpdateTest, BroadcastCallbackReceivesPacket) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    dm.init();

    bool called = false;
    std::vector<uint8_t> captured;
    dm.setBroadcastCallback([&](const uint8_t* pkt, size_t len) {
        called = true;
        captured.assign(pkt, pkt + len);
    });
    dm.forceBroadcast();
    EXPECT_TRUE(called);
    ASSERT_EQ(captured.size(), DiscoveryConfig::DISCOVERY_PACKET_SIZE);
    EXPECT_EQ(captured[4], 1);  // version
}

TEST(DiscoveryResetBackoffTest, SeedsContext) {
    FakeUdp udp; FakeWiFiDiscovery wifi; FakeTime time; FakeSigner signer;
    time.m = 555;
    DiscoveryManager dm(udp, wifi, time, kDeviceId, signer);
    dm.resetBackoff();
    const auto& ctx = dm.getContext();
    EXPECT_EQ(ctx.connectTimeMs, 555u);
    EXPECT_EQ(ctx.lastBroadcastMs, 0u);
    EXPECT_TRUE(ctx.backoffActive);
}

} // namespace
