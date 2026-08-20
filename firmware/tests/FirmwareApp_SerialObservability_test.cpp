// FirmwareApp_SerialObservability_test.cpp - Blind TDD tests for centralized
// serial observability (FirmwareApp owns ONE IEventLogger).
//
// Tests pin the CONTRACT:
//   - FirmwareApp emits [EVENT] lines at every state transition via the injected
//     IEventLogger (no logger injected into any manager).
//   - [STATE] line is enriched with client=<ip|none>, disc=<cadence>, led=<pattern>
//     via FirmwareApp getters passed to LoopHeartbeat::tick().
//
// Assertions are on INTENT (event type + key data present), not exact formatting.

#include "FirmwareApp_test_fixture.h"
#include "ISerialEventLogger.h"
#include "LoopHeartbeat.h"

using ::testing::_;
using ::testing::StrEq;
using ::testing::HasSubstr;
using ::testing::Return;

namespace esp32_firmware {
namespace firmwareapp_test {
namespace {

// ── Mock IEventLogger ─────────────────────────────────────────────────────────
class MockEventLogger : public IEventLogger {
public:
    MOCK_METHOD(void, logEvent, (const char* eventType, const std::string& detail), (override));
    MOCK_METHOD(void, logState, (const std::string& line), (override));
};

// ── Test fixture ──────────────────────────────────────────────────────────────
class FirmwareAppSerialObservabilityTest : public FirmwareAppTest {
protected:
    NiceMock<MockEventLogger> eventLoggerMock;

    void SetUp() override {
        FirmwareAppTest::SetUp();
        // Inject the mock logger before any init/update that might emit events.
        firmwareApp->setEventLogger(eventLoggerMock);
    }

    void TearDown() override {
        FirmwareAppTest::TearDown();
    }

    // Helper: drive init + inject logger in one call.
    void initWithLogger() {
        firmwareApp->init();
    }
};

// ══════════════════════════════════════════════════════════════════════════════
// §1  [EVENT] wifi_connected — emitted when WiFi transitions to WIFI_CONNECTED
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, WiFi_ConnectingToConnected_EmitsWifiConnectedWithIp) {
    initWithLogger();
    // Disable discovery so its broadcast event doesn't interfere with the
    // wifi_connected event expectation on the same update tick.
    firmwareApp->setDiscoveryEnabled(false);

    // Use STA mode so localIP() returns a real address.
    wifiMock.setMode(1);  // WIFI_STA

    // Simulate the WiFi stack reporting STA connected.
    wifiMock.simulateConnectSuccess();

    // Drive the state machine; WiFiManager should transition to WIFI_CONNECTED
    // and FirmwareApp should emit wifi_connected with the local IP.
    EXPECT_CALL(eventLoggerMock, logEvent("wifi_connected", HasSubstr("ip=")));
    firmwareApp->update(5000);
}

// ══════════════════════════════════════════════════════════════════════════════
// §2  [EVENT] wifi_drop — emitted when WiFi disconnects
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, WiFiDisconnected_EmitsWifiDropWithReason) {
    initWithLogger();

    // Simulate a STA disconnect with a specific reason code.
    EXPECT_CALL(eventLoggerMock, logEvent("wifi_drop", HasSubstr("reason=")));
    firmwareApp->onWiFiDisconnected(4);  // WIFI_REASON_DISASSOC_AP_LEAVING
}

// §2b  [EVENT] wifi_drop is DECODED — emits the human-readable reason name, the
// connect phase, and (when associated) the AP BSSID + RSSI. This is the
// observability the field trace needed: a bare "reason=39" from a Deco mesh is
// now named (MESH_STEER_REJECT) and the phase ("assoc") tells at a glance which
// leg of the connect failed. Without decodability the user cannot tell a Deco
// steering reject (reason=8/39) from a wrong-password (202) from a weak-signal
// handshake timeout (204).
TEST_F(FirmwareAppSerialObservabilityTest, WiFiDisconnected_DecodesReasonNamePhaseAndBssid) {
    initWithLogger();
    wifiMock.setMode(1);  // WIFI_STA — so BSSID()/RSSI() return injected values
    wifiMock.setBssid("aa:bb:cc:dd:ee:ff");
    wifiMock.setRssi(-67);

    std::string capturedDetail;
    EXPECT_CALL(eventLoggerMock, logEvent("wifi_drop", _))
        .WillOnce([&](const char*, const std::string& detail) { capturedDetail = detail; });
    firmwareApp->onWiFiDisconnected(39);  // WIFI_REASON_MESH_STEER_REJECT

    EXPECT_THAT(capturedDetail, HasSubstr("reason=39"));
    EXPECT_THAT(capturedDetail, HasSubstr("name=WIFI_REASON_MESH_STEER_REJECT"));
    EXPECT_THAT(capturedDetail, HasSubstr("phase=assoc"));
    EXPECT_THAT(capturedDetail, HasSubstr("bssid=aa:bb:cc:dd:ee:ff"));
    EXPECT_THAT(capturedDetail, HasSubstr("rssi=-67"));
}

// §2c  [EVENT] wifi_drop decodes a handshake-phase timeout (reason=204) without a
// BSSID when not associated — proves the phase labelling and the bssid-only-when
//-associated guard both work for the other dominant field symptom.
TEST_F(FirmwareAppSerialObservabilityTest, WiFiDisconnected_DecodesHandshakePhaseNoBssidWhenUnassociated) {
    initWithLogger();
    wifiMock.setMode(0);  // WIFI_OFF — BSSID() returns empty

    std::string capturedDetail;
    EXPECT_CALL(eventLoggerMock, logEvent("wifi_drop", _))
        .WillOnce([&](const char*, const std::string& detail) { capturedDetail = detail; });
    firmwareApp->onWiFiDisconnected(204);  // WIFI_REASON_HANDSHAKE_TIMEOUT

    EXPECT_THAT(capturedDetail, HasSubstr("name=WIFI_REASON_HANDSHAKE_TIMEOUT"));
    EXPECT_THAT(capturedDetail, HasSubstr("phase=handshake"));
    EXPECT_THAT(capturedDetail, Not(HasSubstr("bssid=")));
}

// ══════════════════════════════════════════════════════════════════════════════
// §3  [EVENT] client_connected — emitted when TCP client authenticates
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, ClientConnected_EmitsEventWithIp) {
    initWithLogger();

    EXPECT_CALL(eventLoggerMock, logEvent("client_connected", StrEq("ip=192.168.1.50")));
    firmwareApp->onClientConnected("192.168.1.50");
    EXPECT_EQ(firmwareApp->getClientIp(), "192.168.1.50");
}

// ══════════════════════════════════════════════════════════════════════════════
// §4  [EVENT] auth_fail — emitted when TCP client fails authentication
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, AuthFailed_EmitsEventWithIpAndReason) {
    initWithLogger();

    EXPECT_CALL(eventLoggerMock,
                logEvent("auth_fail", HasSubstr("ip=192.168.1.50")));
    firmwareApp->onAuthFailed("192.168.1.50");
}

// ══════════════════════════════════════════════════════════════════════════════
// §5  [EVENT] client_disconnected — emitted when TCP client drops
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, ClientDisconnected_EmitsEventWithIpAndReason) {
    initWithLogger();

    // Pre-condition: a client was connected (IP tracked).
    firmwareApp->onClientConnected("192.168.1.50");

    EXPECT_CALL(eventLoggerMock,
                logEvent("client_disconnected", HasSubstr("ip=192.168.1.50")));
    firmwareApp->onClientDisconnected("192.168.1.50", 1);
    EXPECT_TRUE(firmwareApp->getClientIp().empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// §6  [EVENT] discovery_broadcast — emitted when DiscoveryManager sends a packet
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, DiscoveryBroadcast_EmitsEventOnFirstTierOnly) {
    initWithLogger();

    // Drive update() ticks to trigger discovery broadcasts. The first broadcast
    // in a cadence tier emits discovery_broadcast; subsequent broadcasts in the
    // same tier are throttled (no per-broadcast flood at 500ms rapid cadence).
    EXPECT_CALL(udpMock, begin(_)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, beginPacket(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, write(_, _)).Times(AtLeast(1));
    EXPECT_CALL(udpMock, endPacket()).Times(AtLeast(1));

    // First broadcast in rapid tier (500ms) emits the event.
    EXPECT_CALL(eventLoggerMock,
                logEvent("discovery_broadcast", HasSubstr("cadence=")));
    firmwareApp->update(0);
    firmwareApp->update(1000);

    // Subsequent broadcast in the same rapid tier does NOT re-emit.
    EXPECT_CALL(eventLoggerMock, logEvent("discovery_broadcast", _)).Times(0);
    firmwareApp->update(1600);
}

TEST_F(FirmwareAppSerialObservabilityTest, DiscoveryBroadcast_EmitsOnTierChange) {
    initWithLogger();

    // First broadcast at rapid tier (age < 2min) emits the event.
    EXPECT_CALL(eventLoggerMock,
                logEvent("discovery_broadcast", HasSubstr("cadence=500ms")));
    firmwareApp->update(0);
    firmwareApp->update(1000);

    // Advance past the 2min boundary (120000ms) into the next tier (10s).
    // A broadcast after the tier change should emit a new discovery_broadcast
    // event with the new cadence. The broadcast gate fires when
    // now - lastBroadcastMs >= intervalMs; with a 10s interval we need to
    // advance past 120000 + 10000 = 130000ms.
    EXPECT_CALL(eventLoggerMock,
                logEvent("discovery_broadcast", HasSubstr("cadence=10s")));
    firmwareApp->update(130000);
}

// ══════════════════════════════════════════════════════════════════════════════
// §7  [STATE] enrichment — LoopHeartbeat includes client, disc, led fields
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, StateLine_IncludesEnrichedFields) {
    LoopHeartbeat heartbeat(1000);

    // Simulate a tick with enriched fields from FirmwareApp getters.
    ASSERT_TRUE(heartbeat.tick(1000,
                                static_cast<int>(WiFiState::State::WIFI_CONNECTED),
                                false,  // monitor idle
                                "192.168.1.42",
                                "10s",
                                3));  // SOLID_BLUE

    const std::string& snap = heartbeat.snapshot();
    EXPECT_THAT(snap, HasSubstr("client=192.168.1.42"));
    EXPECT_THAT(snap, HasSubstr("disc=10s"));
    EXPECT_THAT(snap, HasSubstr("led=3"));
    EXPECT_THAT(snap, HasSubstr("wifi=WIFI_CONNECTED"));
    EXPECT_THAT(snap, HasSubstr("monitor=idle"));
}

TEST_F(FirmwareAppSerialObservabilityTest, StateLine_ClientNone_WhenNoClient) {
    LoopHeartbeat heartbeat(1000);

    ASSERT_TRUE(heartbeat.tick(1000,
                                static_cast<int>(WiFiState::State::WIFI_CONNECTING),
                                false,
                                "",      // no client
                                "500ms",
                                1));     // PATTERN_SEARCHING

    const std::string& snap = heartbeat.snapshot();
    EXPECT_THAT(snap, HasSubstr("client=none"));
    EXPECT_THAT(snap, HasSubstr("disc=500ms"));
    EXPECT_THAT(snap, HasSubstr("led=1"));
}

// ══════════════════════════════════════════════════════════════════════════════
// §8  No logger injected — emitEvent is a no-op (tests without observability)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, NoLoggerInjected_EmitsDoNotCrash) {
    // Create a fresh FirmwareApp WITHOUT injecting a logger.
    auto app = createFirmwareApp("baked-ssid", "baked-pass");
    app->init();

    // These should not throw even though eventLogger_ is nullptr.
    EXPECT_NO_THROW({
        app->onClientConnected("192.168.1.50");
        app->onAuthFailed("192.168.1.50");
        app->onClientDisconnected("192.168.1.50", 1);
        app->onWiFiDisconnected(4);
    });
}

// ══════════════════════════════════════════════════════════════════════════════
// §9  [EVENT] wifi_ap_fallback — emitted when WiFi escalates to AP mode
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(FirmwareAppSerialObservabilityTest, AuthFail_SingleFailureStaysConnecting_NotApFallback) {
    // CONTRACT (RESILIENT AUTH): a single auth-mechanism failure (15 /
    // 4WAY_HANDSHAKE_TIMEOUT) does NOT immediately escalate to AP mode, so the
    // wifi_ap_fallback [EVENT] must NOT be emitted on the first failure. Instead
    // the manager arms a retry campaign and stays WIFI_CONNECTING, and the
    // [STATE] heartbeat must carry the auth-campaign detail (reason + strategy +
    // loop) so "which SSID, why still connecting" is answerable at a glance.
    initWithLogger();

    // Use STA mode so localIP() returns a real address and the connect
    // transition is observable.
    wifiMock.setMode(1);  // WIFI_STA

    // Establish WIFI_CONNECTED so we have a real STA state to drop from.
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);
    ASSERT_EQ(firmwareApp->getWiFiState(),
              static_cast<int>(WiFiState::State::WIFI_CONNECTED));

    // The campaign detail must be empty before any auth failure.
    ASSERT_EQ(firmwareApp->getWiFiDiagnostic().authCampaignDetail, std::string());

    // Single auth-mechanism failure → stays CONNECTING (no AP fallback yet).
    // onWiFiDisconnected delegates to WiFiManager::onDisconnected (arms the
    // campaign), then update() observes CONNECTING — no wifi_ap_fallback event.
    EXPECT_CALL(eventLoggerMock,
                logEvent("wifi_drop", HasSubstr("reason=15"))).Times(2);
    wifiMock.simulateDisconnect(15);  // model the radio dropping
    firmwareApp->onWiFiDisconnected(15);  // 4WAY_HANDSHAKE_TIMEOUT
    firmwareApp->update(6000);

    EXPECT_EQ(firmwareApp->getWiFiState(),
              static_cast<int>(WiFiState::State::WIFI_CONNECTING))
        << "single auth-mechanism failure must NOT bail to AP";
    EXPECT_THAT(firmwareApp->getWiFiDiagnostic().authCampaignDetail, HasSubstr("reason=15"))
        << "heartbeat must report the active auth campaign detail";
    EXPECT_THAT(firmwareApp->getWiFiDiagnostic().authCampaignDetail, HasSubstr("strategy="))
        << "heartbeat must report the current strategy within the 3-strategy list";
    EXPECT_THAT(firmwareApp->getWiFiDiagnostic().authCampaignDetail, HasSubstr("loop=0/3"))
        << "heartbeat must report the campaign is on the first loop";
}

TEST_F(FirmwareAppSerialObservabilityTest, ApFallbackEmitsCorrectReason_AfterCampaignExhausted) {
    // CONTRACT (RESILIENT AUTH): only AFTER the auth-fail campaign is EXHAUSTED
    // (3 strategies × 3 loops = 9 attempts) does the manager escalate to AP mode
    // and emit [EVENT] wifi_ap_fallback reason=<n> via the injected IEventLogger.
    // The reason is sourced from WiFiManager's ctx.escalatedToApReason field.
    initWithLogger();

    wifiMock.setMode(1);  // WIFI_STA
    wifiMock.simulateConnectSuccess();
    firmwareApp->update(5000);
    ASSERT_EQ(firmwareApp->getWiFiState(),
              static_cast<int>(WiFiState::State::WIFI_CONNECTED));

    // Arm the campaign.
    wifiMock.simulateDisconnect(202);  // model the radio dropping
    firmwareApp->onWiFiDisconnected(202);  // AUTH_FAIL
    firmwareApp->update(6000);
    ASSERT_EQ(firmwareApp->getWiFiState(),
              static_cast<int>(WiFiState::State::WIFI_CONNECTING));

    // The AP-fallback event fires when the transition to AP mode is detected,
    // which happens on the update() that exhausts the campaign.
    EXPECT_CALL(eventLoggerMock,
                logEvent("wifi_ap_fallback", HasSubstr("reason=202")));

    // Exhaust the campaign: 9 retry ticks, each advancing past the 5s interval.
    for (int i = 0; i < 9; ++i) {
        wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
        firmwareApp->update(6000 + (i + 1) * (WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1));
    }

    EXPECT_EQ(firmwareApp->getWiFiState(),
              static_cast<int>(WiFiState::State::WIFI_AP_MODE_AUTH_FAIL))
        << "after 3 full loops the campaign must escalate to the AUTH_FAIL AP state";
}

} // namespace
} // namespace firmwareapp_test
} // namespace esp32_firmware
