// WiFiManager_test.cpp - Tests for WiFiManager vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/WiFiManager.h"
#include "mocks/WiFiMock.h"
#include "mocks/PreferencesMock.h"
#include "mocks/ArduinoMock.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class WiFiManagerTest : public ::testing::Test {
protected:
    WiFiMock wifiMock;
    PreferencesMock prefsMock;
    std::unique_ptr<WiFiManager> wifiManager;

    void SetUp() override {
        wifiMock.reset();
        prefsMock.reset();
        arduino_mock::resetAllMocks();

        wifiManager = std::make_unique<WiFiManager>(
            wifiMock, prefsMock,
            "baked-ssid", "baked-pass"
        );
    }

    void TearDown() override {
        wifiManager.reset();
    }
};

TEST_F(WiFiManagerTest, DetermineCredentialSource_StoredNVS_ReturnsStoredNVS) {
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    CredentialSource source = determineCredentialSource(prefsMock, nullptr, nullptr);
    EXPECT_EQ(source, CredentialSource::STORED_NVS);
}

TEST_F(WiFiManagerTest, DetermineCredentialSource_EmptyNVS_ReturnsNone) {
    CredentialSource source = determineCredentialSource(prefsMock, nullptr, nullptr);
    EXPECT_EQ(source, CredentialSource::NONE);
}

TEST_F(WiFiManagerTest, DetermineCredentialSource_BakedCredentials_ReturnsBakedIn) {
    CredentialSource source = determineCredentialSource(prefsMock, "baked-ssid", "baked-pass");
    EXPECT_EQ(source, CredentialSource::BAKED_IN);
}

TEST_F(WiFiManagerTest, DetermineCredentialSource_StoredNVSPreferOverBaked_ReturnsStoredNVS) {
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    CredentialSource source = determineCredentialSource(prefsMock, "baked-ssid", "baked-pass");
    EXPECT_EQ(source, CredentialSource::STORED_NVS);
}

TEST_F(WiFiManagerTest, ShouldFallbackToApMode_StoredNVSAndTimeout_ReturnsTrue) {
    bool result = shouldFallbackToApMode(
        CredentialSource::STORED_NVS,
        WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1
    );
    EXPECT_TRUE(result);
}

TEST_F(WiFiManagerTest, ShouldFallbackToApMode_StoredNVSNoTimeout_ReturnsFalse) {
    bool result = shouldFallbackToApMode(
        CredentialSource::STORED_NVS,
        WiFiConfig::WIFI_CONNECT_TIMEOUT_MS - 1
    );
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, ShouldFallbackToApMode_BakedIn_ReturnsFalse) {
    bool result = shouldFallbackToApMode(
        CredentialSource::BAKED_IN,
        WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1
    );
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, IsInitialConnectTimeout_ExceedsMaxRetries_ReturnsTrue) {
    uint32_t duration = (WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS) + 1;
    bool result = isInitialConnectTimeout(duration);
    EXPECT_TRUE(result);
}

TEST_F(WiFiManagerTest, IsInitialConnectTimeout_WithinMaxRetries_ReturnsFalse) {
    uint32_t duration = (WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS) - 1;
    bool result = isInitialConnectTimeout(duration);
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, ShouldRetryWiFi_DisconnectedState_ReturnsTrueAfterInterval) {
    uint32_t now = WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    bool result = shouldRetryWiFi(
        WiFiState::State::WIFI_DISCONNECTED, now, 0
    );
    EXPECT_TRUE(result);
}

TEST_F(WiFiManagerTest, ShouldRetryWiFi_ConnectedState_ReturnsFalse) {
    bool result = shouldRetryWiFi(
        WiFiState::State::WIFI_CONNECTED, 10000, 0
    );
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, LoadCredentials_ValidCredentials_ReturnsTrue) {
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    std::string ssid, pass;
    bool result = wifiManager->loadCredentials(ssid, pass);
    EXPECT_TRUE(result);
    EXPECT_EQ(ssid, "test-ssid");
    EXPECT_EQ(pass, "test-pass");
}

TEST_F(WiFiManagerTest, LoadCredentials_NoCredentials_ReturnsFalse) {
    std::string ssid, pass;
    bool result = wifiManager->loadCredentials(ssid, pass);
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, StoreCredentials_ValidCredentials_ReturnsTrue) {
    bool result = wifiManager->storeCredentials("new-ssid", "new-pass");
    EXPECT_TRUE(result);
    EXPECT_EQ(prefsMock.getValue("wifi", "ssid"), "new-ssid");
    EXPECT_EQ(prefsMock.getValue("wifi", "pass"), "new-pass");
}

TEST_F(WiFiManagerTest, ClearCredentials_RemovesCredentials) {
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    bool result = wifiManager->clearCredentials();
    EXPECT_TRUE(result);
    EXPECT_FALSE(prefsMock.hasKey("wifi", "ssid"));
    EXPECT_FALSE(prefsMock.hasKey("wifi", "pass"));
}

TEST_F(WiFiManagerTest, FactoryReset_CallsClearCredentials) {
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    bool result = wifiManager->factoryReset();
    EXPECT_TRUE(result);
    EXPECT_FALSE(prefsMock.hasKey("wifi", "ssid"));
}

TEST_F(WiFiManagerTest, Init_SetsInitialStateToDisconnected) {
    // Note: init() transitions to AP mode if no credentials available
    // because DisconnectedStateHandler transitions to CONNECTED_AP when
    // there are no stored or baked credentials
    wifiManager->init();
    EXPECT_NE(wifiManager->getState(), WiFiState::State::WIFI_DISCONNECTED);
}

TEST_F(WiFiManagerTest, StateName_ReturnsCorrectNames) {
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_DISCONNECTED), "WIFI_DISCONNECTED");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_CONNECTING), "WIFI_CONNECTING");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_CONNECTED), "WIFI_CONNECTED");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_AP_MODE), "WIFI_AP_MODE");
    EXPECT_STREQ(WiFiManager::stateName(static_cast<WiFiState::State>(99)), "UNKNOWN");
}

TEST_F(WiFiManagerTest, HasStoredCredentials_ReturnsTrueWhenCredentialsExist) {
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    EXPECT_TRUE(wifiManager->hasStoredCredentials());
}

TEST_F(WiFiManagerTest, HasStoredCredentials_ReturnsFalseWhenNoCredentials) {
    EXPECT_FALSE(wifiManager->hasStoredCredentials());
}

TEST_F(WiFiManagerTest, OnDisconnected_NonAuthFailure_SetsConnectingState) {
    // This test verifies that when WiFi disconnects from WIFI_CONNECTED state
    // due to NON-AUTH failures (e.g., beacon timeout), the state machine
    // transitions to WIFI_CONNECTING (RECONNECTING merged) and sets
    // tcpServerNeedsRestart.

    // Set up stored credentials
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    // Re-create WiFiManager with fresh prefs after setting credentials
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock,
        "baked-ssid", "baked-pass"
    );

    // Drive through to WIFI_CONNECTED: init → WIFI_CONNECTING → update → WIFI_CONNECTED
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Simulate disconnect event with NON-AUTH failure (e.g., beacon timeout)
    // This should transition to WIFI_CONNECTING (RECONNECTING merged into WIFI_CONNECTING)
    wifiManager->onDisconnected(200);  // WIFI_REASON_BEACON_TIMEOUT

    // Verify state is now WIFI_CONNECTING (non-auth failures retry indefinitely)
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Verify TCP server restart flag is set
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer());
}

TEST_F(WiFiManagerTest, OnDisconnected_AuthFail_TransitionsToApMode) {
    // This test verifies that AUTH_FAIL (and related auth errors) immediately
    // transition to AP mode instead of retrying indefinitely

    // Set up stored credentials
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    // Re-create WiFiManager with fresh prefs after setting credentials
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock,
        "baked-ssid", "baked-pass"
    );

    // Get to CONNECTED_STA state
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Simulate disconnect event with AUTH_FAIL - this should transition to AP mode
    wifiManager->onDisconnected(202);  // WIFI_REASON_AUTH_FAIL

    // Verify state is now CONNECTED_AP (auth-fail transitions to AP mode)
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);

    // TCP server restart flag should NOT be set for AP mode transition
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer());
}

// ── State-handler body coverage (Disconnected / Connecting branches) ─────────────────
// The pure helpers above are well-tested; these tests drive the state-machine
// handler BODIES via update() — the Disconnected NONE→AP fallback, the
// Connecting CONNECT_FAILED retry + AP fallback, and the initial-connect
// timeout branches (STORED_NVS / BAKED_IN) plus the Reconnecting recovery.

TEST_F(WiFiManagerTest, Init_NoCredentials_TransitionsToApMode) {
    // DisconnectedStateHandler NONE branch: no stored NVS creds AND no baked
    // credentials → setMode(AP) + softAP() → CONNECTED_AP.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock,
        nullptr, nullptr);  // no baked creds

    wifiManager->init();

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
    EXPECT_EQ(wifiMock.getApSsid(), std::string(WiFiConfig::AP_SSID));
}

TEST_F(WiFiManagerTest, Connecting_ConnectFailedAndTimeout_FallsBackToApMode) {
    // ConnectingStateHandler: status==CONNECT_FAILED + connectDuration past the
    // WIFI_CONNECT_TIMEOUT_MS threshold → shouldFallbackToApMode true → AP mode.
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);  // status 4
    wifiManager->init();
    // Disconnected(STORED_NVS) → CONNECTING; connectStartTime set at init time.
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // init()'s Disconnected handler called wifi_.begin(stored), which the mock
    // resets to WL_IDLE_STATUS — so re-assert the failed status AFTER init to
    // model a real connection attempt that has failed.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    // Advance past the connect timeout (30s) so shouldFallbackToApMode() is true.
    wifiManager->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1000);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
}

TEST_F(WiFiManagerTest, Connecting_ConnectFailedBeforeTimeout_RetriesStoredCredentials) {
    // ConnectingStateHandler: status==CONNECT_FAILED but within the timeout →
    // no AP fallback; shouldRetryWiFi triggers a disconnect+begin retry using
    // the STORED_NVS credentials. State stays CONNECTING.
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // init()'s Disconnected handler called wifi_.begin(stored), which the mock
    // resets to WL_IDLE_STATUS — re-assert the failed status AFTER init.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    // Advance past the retry interval (5s) but well within the 30s timeout.
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    // Still attempting (CONNECTING), not fallen back to AP.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    // The retry path disconnected then re-began with the stored SSID.
    EXPECT_EQ(wifiMock.getCurrentSsid(), std::string("real-ssid"));
}

TEST_F(WiFiManagerTest, Connecting_InitialTimeoutWithStoredCredentials_FallsBackToApMode) {
    // ConnectingStateHandler isInitialConnectTimeout branch: status neither
    // CONNECTED nor CONNECT_FAILED/NO_SSID (idle), and connectDuration exceeds
    // the initial-connect budget → STORED_NVS → AP fallback.
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);  // neither 3 nor 4/1
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Past the initial-connect budget (60 retries * 5s = 300s).
    const uint32_t kInitialBudgetMs =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    wifiManager->update(kInitialBudgetMs + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
}

TEST_F(WiFiManagerTest, Connecting_InitialTimeoutWithBakedCredentials_TransitionsToConnecting) {
    // isInitialConnectTimeout branch for BAKED_IN: baked creds should "just
    // work", so on initial timeout the handler stays in WIFI_CONNECTING
    // (RECONNECTING merged into WIFI_CONNECTING; retry loop continues) rather
    // than falling back to AP.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    const uint32_t kInitialBudgetMs =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    wifiManager->update(kInitialBudgetMs + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
}

TEST_F(WiFiManagerTest, Connecting_WhenWifiConnected_TransitionsToConnected) {
    // ConnectingStateHandler body (covers former ReconnectingStateHandler path):
    // once status() reports WL_CONNECTED while in WIFI_CONNECTING, the handler
    // transitions back to WIFI_CONNECTED (tcpRestart + ntp init).
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, nullptr, nullptr);

    // Reach WIFI_CONNECTING through a real connect + non-auth drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();                                   // → WIFI_CONNECTING
    // init()'s Disconnected handler called wifi_.begin(), which the mock resets
    // to WL_IDLE_STATUS — re-assert connected so the next tick observes it.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);                              // → WIFI_CONNECTED
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT → WIFI_CONNECTING
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // While WIFI_CONNECTING, WiFi comes back → WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(1000);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
}

// ── State-handler body coverage: remaining reachable branches ───────────────
// Three uncovered handler paths that ARE reachable production behaviour (as
// opposed to the dead/defensive branches — see chunk-2 report). Each drives a
// real transition the field relies on.

TEST_F(WiFiManagerTest, Connecting_ConnectFailedWithBakedCreds_RetriesBakedCredentials) {
    // ConnectingStateHandler retry branch: status==CONNECT_FAILED, within the
    // 30s timeout (no AP fallback), no STORED_NVS credentials → the retry
    // falls through to the baked-credentials begin() (the `else if (baked)`
    // arm). State stays CONNECTING; the re-attempt uses the baked SSID.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Re-assert failed status after init (begin() resets the mock), then
    // advance past the retry interval but within the timeout.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    // The retry re-began with the BAKED SSID (no stored creds to use).
    EXPECT_EQ(wifiMock.getCurrentSsid(), std::string("baked-ssid"));
}

// Covers shouldFallbackToApMode with NONE source: never falls back to AP
// (only STORED_NVS with elapsed timeout falls back).
TEST_F(WiFiManagerTest, ShouldFallbackToApMode_None_ReturnsFalse) {
    bool result = shouldFallbackToApMode(
        CredentialSource::NONE,
        WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1
    );
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, Connecting_NotConnectedAfterInterval_RetriesBakedCredentials) {
    // ConnectingStateHandler retry branch (merged from ReconnectingStateHandler):
    // still not connected, the retry interval has elapsed → begin() with baked
    // creds (no stored creds present). Pins the retry body.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    // Reach WIFI_CONNECTING via a real connect + non-auth drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT → WIFI_CONNECTING
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Still not connected; advance past the retry interval so shouldRetryWiFi
    // fires the begin() retry with the baked SSID (no stored creds present).
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    // State stays WIFI_CONNECTING (retry doesn't change it); the re-attempt used
    // the baked SSID.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_EQ(wifiMock.getCurrentSsid(), std::string("baked-ssid"));
}

// Covers the ConnectingStateHandler retry branch (merged from ReconnectingStateHandler)
// with STORED_NVS creds: hits wifi_.begin(storedSsid, storedPass) when loadCredentialsImpl succeeds.
TEST_F(WiFiManagerTest, Connecting_NotConnectedAfterInterval_RetriesStoredCredentials) {
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, nullptr, nullptr);

    // Reach WIFI_CONNECTING via a real connect + non-auth drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT → WIFI_CONNECTING
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Still not connected; advance past the retry interval so shouldRetryWiFi
    // fires the begin() retry with the stored SSID (loadCredentialsImpl succeeds).
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    // State stays WIFI_CONNECTING; the re-attempt used the stored SSID.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_EQ(wifiMock.getCurrentSsid(), std::string("real-ssid"));
}

TEST_F(WiFiManagerTest, ConnectedSta_DroppedConnection_TransitionsToConnecting) {
    // ConnectedStaStateHandler body: a STA drop observed on the state-machine
    // tick (status() != WL_CONNECTED while in WIFI_CONNECTED) transitions to
    // WIFI_CONNECTING with the tcp-restart flag. Distinct from onDisconnected()
    // (event-callback driven): this is the per-tick self-heal that catches
    // drops the WiFi event callback did not surface. RECONNECTING was merged
    // into WIFI_CONNECTING (spec §1).
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // The STA link drops between ticks — the next update() observes it.
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer());
}

// ══════════════════════════════════════════════════════════════════════════════
// Commit 6: #4 WiFi retry + AP serial event — auth→AP gate tests
//
// USER POLICY (strict): AP mode ONLY for definitive auth failure (wrong
// password — the credentials are definitively rejected; it can NEVER work).
// EVERYTHING else → STA retry FOREVER (router reboot, AUTH_EXPIRE, SSID
// temporarily gone — all transient, all might recover).
//
// Definitive auth reasons (→ AP mode):
//   - 4WAY_HANDSHAKE_TIMEOUT (15): handshake never completed (bad PSK-class)
//   - 802_1X_AUTH_FAILED (23): enterprise auth rejected
//   - AUTH_FAIL (202): bad PSK
//
// Transient reasons (→ STA retry forever):
//   - AUTH_EXPIRE (2), AUTH_LEAVE (3), ASSOC_EXPIRE (4), BEACON_TIMEOUT (200),
//     reason=0/4/204 etc.
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(WiFiManagerTest, wrongPasswordGoesToAp_4WayHandshakeTimeout) {
    // 4WAY_HANDSHAKE_TIMEOUT (15) is a definitive auth failure: the PSK was
    // cryptographically refused. Must transition to AP mode immediately,
    // NOT retry STA forever.
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    wifiManager->onDisconnected(15);  // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer())
        << "AP mode transition must clear the TCP restart flag";
    EXPECT_EQ(wifiManager->getContext().escalatedToApReason, 15)
        << "escalatedToApReason must record the definitive-auth reason";
}

TEST_F(WiFiManagerTest, authFailGoesToAp_AuthFail202) {
    // AUTH_FAIL (202) is a definitive auth failure: bad PSK. Must transition
    // to AP mode immediately, NOT retry STA forever.
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    wifiManager->onDisconnected(202);  // WIFI_REASON_AUTH_FAIL

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer())
        << "AP mode transition must clear the TCP restart flag";
    EXPECT_EQ(wifiManager->getContext().escalatedToApReason, 202)
        << "escalatedToApReason must record the definitive-auth reason";
}

TEST_F(WiFiManagerTest, authExpireRetriesStaForever_Reason2StaysConnecting) {
    // AUTH_EXPIRE (2) is a TRANSIENT session-lifecycle reason: the auth
    // session timed out, but the credentials are still valid. Must NOT
    // transition to AP mode — must retry STA forever (re-enter CONNECTING).
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    wifiManager->onDisconnected(2);  // WIFI_REASON_AUTH_EXPIRE

    // Must stay in CONNECTING (retry), NOT AP mode.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer())
        << "transient drop must keep the TCP restart flag armed (link rebuild)";
    EXPECT_EQ(wifiManager->getContext().escalatedToApReason, 0)
        << "transient reason must NOT set escalatedToApReason";

    // Advance past the retry interval and verify the retry loop continues
    // (stays CONNECTING, does NOT bail to AP mode). This pins "retry forever".
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "AUTH_EXPIRE must retry STA forever, not escalate to AP";

    // Advance past another retry interval — still retrying.
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS * 2 + 1);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "AUTH_EXPIRE must keep retrying STA across multiple intervals";
}

TEST_F(WiFiManagerTest, routerRebootRetriesSta_AssocExpireReason4) {
    // ASSOC_EXPIRE (4) is a TRANSIENT reason: the router rebooted or the
    // association expired. Must NOT transition to AP mode — must retry STA.
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    wifiManager->onDisconnected(4);  // WIFI_REASON_ASSOC_EXPIRE

    // Must stay in CONNECTING (retry), NOT AP mode.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer())
        << "transient drop must keep the TCP restart flag armed (link rebuild)";
    EXPECT_EQ(wifiManager->getContext().escalatedToApReason, 0)
        << "transient reason must NOT set escalatedToApReason";
}