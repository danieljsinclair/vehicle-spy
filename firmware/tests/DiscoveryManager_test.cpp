// DiscoveryManager_test.cpp - Tests for DiscoveryManager vanilla class

#include <gtest/gtest.h>
#include <cstring>
#include "vanilla/DiscoveryManager.h"
#include "mocks/WiFiMock.h"
#include "mocks/ArduinoMock.h"

using namespace esp32_firmware;

class MockUdp : public IUdp {
public:
    bool beginCalled = false;
    uint16_t beginPort = 0;
    std::string lastPacketIp;
    uint16_t lastPacketPort = 0;
    std::vector<uint8_t> lastPacketData;
    size_t lastPacketLen = 0;
    int beginPacketResult = 1;
    int endPacketResult = 1;

    void begin(uint16_t port) override {
        beginCalled = true;
        beginPort = port;
    }

    int beginPacket(const std::string& ip, uint16_t port) override {
        lastPacketIp = ip;
        lastPacketPort = port;
        return beginPacketResult;
    }

    size_t write(const uint8_t* data, size_t len) override {
        lastPacketData.assign(data, data + len);
        lastPacketLen = len;
        return len;
    }

    int endPacket() override {
        return endPacketResult;
    }

    void reset() {
        beginCalled = false;
        beginPort = 0;
        lastPacketIp.clear();
        lastPacketPort = 0;
        lastPacketData.clear();
        lastPacketLen = 0;
        beginPacketResult = 1;
        endPacketResult = 1;
    }
};

class MockTime : public ITime {
public:
    uint64_t timestamp = 1000000000;  // Valid Unix timestamp
    uint32_t millisValue = 0;

    uint64_t getCurrentTimestamp() const override {
        return timestamp;
    }

    uint32_t millis() const override {
        return millisValue;
    }

    void setTimestamp(uint64_t t) { timestamp = t; }
    void setMillis(uint32_t m) { millisValue = m; }
    void advanceMillis(uint32_t m) { millisValue += m; }
};

class DiscoveryManagerTest : public ::testing::Test {
protected:
    MockUdp udpMock;
    WiFiMock wifiMock;
    MockTime timeMock;
    std::array<uint8_t, 16> deviceId = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    NullDiscoverySigner nullSigner;
    std::unique_ptr<DiscoveryManager> discoveryManager;
    std::vector<uint8_t> capturedPacket;
    size_t capturedLen = 0;

    void SetUp() override {
        udpMock.reset();
        wifiMock.reset();
        timeMock.setMillis(0);
        timeMock.setTimestamp(1000000000);

        capturedPacket.clear();
        capturedLen = 0;

        discoveryManager = std::make_unique<DiscoveryManager>(
            udpMock, wifiMock, timeMock, deviceId, nullSigner
        );

        discoveryManager->setBroadcastCallback([this](const uint8_t* packet, size_t len) {
            capturedPacket.assign(packet, packet + len);
            capturedLen = len;
        });
    }

    void TearDown() override {
        discoveryManager.reset();
    }
};

TEST_F(DiscoveryManagerTest, Init_StartsUDP) {
    discoveryManager->init();
    EXPECT_TRUE(udpMock.beginCalled);
    EXPECT_EQ(udpMock.beginPort, DiscoveryConfig::DISCOVERY_PORT);
}

TEST_F(DiscoveryManagerTest, DiscoveryIntervalMs_RapidPeriod_0_2Min) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(0),
              DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(90000),
              DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(119999),
              DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS);
}

TEST_F(DiscoveryManagerTest, DiscoveryIntervalMs_2_5MinPeriod) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(DiscoveryConfig::DISCOVERY_AGE_2_MIN_MS),
              DiscoveryConfig::DISCOVERY_INTERVAL_2_5_MIN_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(DiscoveryConfig::DISCOVERY_AGE_5_MIN_MS - 1),
              DiscoveryConfig::DISCOVERY_INTERVAL_2_5_MIN_MS);
}

TEST_F(DiscoveryManagerTest, DiscoveryIntervalMs_5_10MinPeriod) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(DiscoveryConfig::DISCOVERY_AGE_5_MIN_MS),
              DiscoveryConfig::DISCOVERY_INTERVAL_5_10_MIN_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(DiscoveryConfig::DISCOVERY_AGE_10_MIN_MS - 1),
              DiscoveryConfig::DISCOVERY_INTERVAL_5_10_MIN_MS);
}

TEST_F(DiscoveryManagerTest, DiscoveryIntervalMs_SlowPeriod_GreaterThan10Min) {
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(DiscoveryConfig::DISCOVERY_AGE_10_MIN_MS),
              DiscoveryConfig::DISCOVERY_INTERVAL_SLOW_MS);
    EXPECT_EQ(DiscoveryManager::discoveryIntervalMs(720000),
              DiscoveryConfig::DISCOVERY_INTERVAL_SLOW_MS);
}

TEST_F(DiscoveryManagerTest, BuildDiscoveryPacket_CorrectFormat) {
    std::array<uint8_t, DiscoveryConfig::DISCOVERY_PACKET_SIZE> packet;
    DiscoveryManager::buildDiscoveryPacket(packet.data(), deviceId.data(),
                                           DiscoveryConfig::TCP_PORT, DiscoveryConfig::OTA_HTTP_PORT,
                                           1000000000, 12345);

    // Check magic "VSIM"
    EXPECT_EQ(packet[0], 0x56);
    EXPECT_EQ(packet[1], 0x53);
    EXPECT_EQ(packet[2], 0x49);
    EXPECT_EQ(packet[3], 0x4D);

    // Check version
    EXPECT_EQ(packet[4], 1);

    // Check packet type
    EXPECT_EQ(packet[5], 1);

    // Check device ID (16 bytes at offset 6)
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(packet[6 + i], deviceId[i]) << "Device ID byte " << i;
    }

    // Check nonce at offset 22 (4 bytes, host-endian memcpy of uint32).
    // Decode the field as a value rather than pinning individual bytes, so the
    // test survives a refactor of how the nonce is serialized (memcpy vs loop),
    // as long as the wire value is 12345.
    uint32_t decodedNonce = 0;
    std::memcpy(&decodedNonce, packet.data() + 22, 4);
    EXPECT_EQ(decodedNonce, 12345u);

    // Check timestamp at offset 30 (8 bytes, big-endian Unix epoch per
    // DiscoveryManager.cpp). Decode as a BE uint64 value rather than pinning
    // bytes, so the test locks the WIRE FORMAT (BE timestamp == 1000000000)
    // not the serialization loop shape.
    uint64_t decodedTimestamp = 0;
    for (int i = 0; i < 8; ++i) {
        decodedTimestamp = (decodedTimestamp << 8) | packet[30 + i];
    }
    EXPECT_EQ(decodedTimestamp, 1000000000ULL);

    // Check TCP port (3333 = 0x0D05) at offset 38
    EXPECT_EQ(packet[38], 0x0D);
    EXPECT_EQ(packet[39], 0x05);

    // Check OTA port (80 = 0x0050) at offset 40
    EXPECT_EQ(packet[40], 0x00);
    EXPECT_EQ(packet[41], 0x50);

    // Check signature is zeroed
    for (int i = 42; i < 106; i++) {
        EXPECT_EQ(packet[i], 0) << "Signature byte " << (i - 42) << " should be zero";
    }
}

TEST_F(DiscoveryManagerTest, ResetBackoff_SetsConnectTime) {
    timeMock.setMillis(5000);
    discoveryManager->resetBackoff();

    EXPECT_EQ(discoveryManager->getContext().connectTimeMs, 5000);
    EXPECT_EQ(discoveryManager->getContext().lastBroadcastMs, 0);
    EXPECT_TRUE(discoveryManager->getContext().backoffActive);
}

TEST_F(DiscoveryManagerTest, Update_NoClient_APMode_Broadcasts) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    timeMock.setMillis(1000);
    discoveryManager->update(1000, false);  // no client

    EXPECT_TRUE(udpMock.beginPacketResult == 1);
    EXPECT_EQ(capturedLen, DiscoveryConfig::DISCOVERY_PACKET_SIZE);
}

TEST_F(DiscoveryManagerTest, Update_HasClient_StillBroadcasts) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    timeMock.setMillis(1000);
    discoveryManager->update(1000, true);  // has client — still broadcasts (always discoverable)

    EXPECT_GT(capturedLen, 0);
}

TEST_F(DiscoveryManagerTest, Update_IntervalNotElapsed_DoesNotBroadcast) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    // init() sets connectTimeMs = timeMock.millis() = 0, lastBroadcastMs = 0
    // Fast interval is 500ms
    // First update at 100ms - interval NOT elapsed (100 - 0 = 100 < 500)
    timeMock.setMillis(100);
    discoveryManager->update(100, false);

    // No broadcast should happen yet
    EXPECT_EQ(capturedLen, 0);

    // Second update at 200ms - still interval NOT elapsed (200 - 0 = 200 < 500)
    discoveryManager->update(200, false);

    // Still no broadcast
    EXPECT_EQ(capturedLen, 0);

    // Third update at 500ms - interval elapsed (500 - 0 = 500 >= 500)
    timeMock.setMillis(500);
    discoveryManager->update(500, false);

    // Now broadcast should happen
    EXPECT_EQ(capturedLen, DiscoveryConfig::DISCOVERY_PACKET_SIZE);
}

// Covers lines 19-20: update() returns early when shouldBroadcast() is false
// (WiFi mode neither AP nor STA and no client connected).
TEST_F(DiscoveryManagerTest, Update_ShouldBroadcastFalse_ReturnsEarly) {
    wifiMock.setMode(0);  // neither AP (2) nor STA (1) -> shouldBroadcast returns false
    discoveryManager->init();
    timeMock.setMillis(1000);
    discoveryManager->update(1000, false);  // haveClient=false
    EXPECT_TRUE(udpMock.lastPacketIp.empty());  // beginPacket never called
}

// Covers line 85: broadcast() null-callback else branch (no callback registered).
TEST_F(DiscoveryManagerTest, Broadcast_NoCallback_DoesNotCrash) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    // Create manager without registering a callback to hit the null branch.
    DiscoveryManager dm(udpMock, wifiMock, timeMock, deviceId, nullSigner);
    dm.init();
    dm.forceBroadcast();
    EXPECT_GT(udpMock.lastPacketData.size(), 0);  // UDP packet still sent
}

TEST_F(DiscoveryManagerTest, ForceBroadcast_SendsPacket) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    discoveryManager->forceBroadcast();

    EXPECT_EQ(capturedLen, DiscoveryConfig::DISCOVERY_PACKET_SIZE);
}

TEST_F(DiscoveryManagerTest, Broadcast_UsesCorrectBroadcastIP) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    timeMock.setMillis(1000);
    discoveryManager->update(1000, false);

    EXPECT_EQ(udpMock.lastPacketIp, "192.168.4.255");
    EXPECT_EQ(udpMock.lastPacketPort, DiscoveryConfig::DISCOVERY_PORT);
}

// ── Defect 1 (RED): broadcast() must NOT count / fire callback on send failure ──
// If beginPacket() returns a failure code, the packet never leaves the radio.
// The old code incremented broadcastCount_ BEFORE send and ignored the return
// values of beginPacket()/endPacket(), so a failed send was counted as success
// and the callback fired on a phantom packet. This is the unit-level proof of
// the tcpdump anomaly: broadcast() called ~264× but the packet left ~1×.
TEST_F(DiscoveryManagerTest, Broadcast_BeginPacketFails_DoesNotCountOrCallback) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    // Simulate UDP send failing at beginPacket().
    udpMock.beginPacketResult = 0;

    timeMock.setMillis(1000);
    discoveryManager->update(1000, false);

    // Send failed → must NOT count, must NOT fire callback, must NOT have
    // delivered a packet to the wire (write).
    EXPECT_EQ(discoveryManager->broadcastCount(), 0u);
    EXPECT_EQ(capturedLen, 0u);
    EXPECT_EQ(udpMock.lastPacketData.size(), 0u);
}

TEST_F(DiscoveryManagerTest, Broadcast_EndPacketFails_DoesNotCountOrCallback) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    // beginPacket succeeds but the final flush (endPacket) fails.
    udpMock.beginPacketResult = 1;
    udpMock.endPacketResult = 0;

    timeMock.setMillis(1000);
    discoveryManager->update(1000, false);

    EXPECT_EQ(discoveryManager->broadcastCount(), 0u);
    EXPECT_EQ(capturedLen, 0u);
}

// ── Sustained cadence / "packet actually sent via mock" (RED → GREEN) ──────────
// The user explicitly demanded a unit test proving the packet is genuinely
// emitted over time at the correct fast-tier cadence, with the real bytes on
// the wire (magic VSIM + correct length). We advance a MockTime across >2 min
// and assert MULTIPLE broadcasts at 500ms, not merely AtLeast(1) once.
TEST_F(DiscoveryManagerTest, SustainedCadence_MultipleBroadcastsWithRealBytes) {
    wifiMock.setMode(2);  // WIFI_AP
    wifiMock.softAP("ESP32-CAN", "cancan12");
    discoveryManager->init();

    // Walk time forward in 500ms ticks from t=0 to t=130000ms (>2 min), calling
    // update() each tick. The fast tier (0-2min) is 500ms, so we expect a
    // broadcast at t=500,1000,...,125000 → ~250 packets, plus t=130000 falls in
    // the 2-5min tier (10s) so it does NOT immediately fire at +500.
    // Tick at the fast-tier period itself so each elapsed period can fire.
    constexpr uint32_t kTickMs = DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS;
    constexpr uint32_t kWalkEndMs = DiscoveryConfig::DISCOVERY_AGE_2_MIN_MS + 10000;
    uint32_t now = 0;
    for (uint32_t t = 0; t <= kWalkEndMs; t += kTickMs) {
        discoveryManager->update(now, false);
        now += kTickMs;
    }

    // The LAST packet handed to the UDP mock is a full, well-formed discovery
    // packet — proof real bytes reached the wire, not just that a counter moved.
    ASSERT_EQ(udpMock.lastPacketData.size(), DiscoveryConfig::DISCOVERY_PACKET_SIZE);
    // Magic "VSIM" (offsets pinned by BuildDiscoveryPacket_CorrectFormat).
    EXPECT_EQ(udpMock.lastPacketData[0], 0x56);
    EXPECT_EQ(udpMock.lastPacketData[1], 0x53);
    EXPECT_EQ(udpMock.lastPacketData[2], 0x49);
    EXPECT_EQ(udpMock.lastPacketData[3], 0x4D);

    // Pin the CADENCE, not merely "more than once". Across the 0-2min window one
    // broadcast is due per fast-tier period, then the 2-5min tier slows to one
    // per 10 s. Deriving the expectation from the config constants keeps this
    // honest if a tier is retuned, and — crucially — makes it able to FAIL: a
    // loose `> 100` would still pass if the fast tier silently doubled to 1 s
    // (~120 broadcasts), which is exactly the regression this test must catch.
    constexpr uint32_t kFastBroadcasts =
        DiscoveryConfig::DISCOVERY_AGE_2_MIN_MS /
        DiscoveryConfig::DISCOVERY_INTERVAL_FAST_MS;              // 240
    constexpr uint32_t kSlowBroadcasts =
        10000 / DiscoveryConfig::DISCOVERY_INTERVAL_2_5_MIN_MS;   // 1
    EXPECT_EQ(discoveryManager->broadcastCount(),
              kFastBroadcasts - 1 + kSlowBroadcasts)
        << "sustained fast-tier cadence must hold across the 0-2min window "
           "(one broadcast per DISCOVERY_INTERVAL_FAST_MS), then slow to the "
           "2-5min tier";
}