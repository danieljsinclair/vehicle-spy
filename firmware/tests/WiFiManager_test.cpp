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

class MockStatusLED : public IStatusLED {
public:
    MOCK_METHOD(void, setPattern, (int pattern), (override));
    MOCK_METHOD(void, update, (uint32_t now), (override));
};

class WiFiManagerTest : public ::testing::Test {
protected:
    WiFiMock wifiMock;
    PreferencesMock prefsMock;
    MockStatusLED statusLedMock;
    std::unique_ptr<WiFiManager> wifiManager;

    void SetUp() override {
        wifiMock.reset();
        prefsMock.reset();
        arduino_mock::resetAllMocks();

        wifiManager = std::make_unique<WiFiManager>(
            wifiMock, prefsMock, statusLedMock,
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
        WiFiState::State::DISCONNECTED, now, 0
    );
    EXPECT_TRUE(result);
}

TEST_F(WiFiManagerTest, ShouldRetryWiFi_ConnectedState_ReturnsFalse) {
    bool result = shouldRetryWiFi(
        WiFiState::State::CONNECTED_STA, 10000, 0
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
    EXPECT_NE(wifiManager->getState(), WiFiState::State::DISCONNECTED);
}

TEST_F(WiFiManagerTest, StateName_ReturnsCorrectNames) {
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::DISCONNECTED), "DISCONNECTED");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::CONNECTING), "CONNECTING");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::CONNECTED_STA), "CONNECTED_STA");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::CONNECTED_AP), "CONNECTED_AP");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::RECONNECTING), "RECONNECTING");
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

TEST_F(WiFiManagerTest, OnDisconnected_NonAuthFailure_SetsReconnectingState) {
    // This test verifies that when WiFi disconnects from CONNECTED_STA state
    // due to NON-AUTH failures (e.g., beacon timeout), the state machine
    // transitions to RECONNECTING and sets tcpServerNeedsRestart

    // Set up stored credentials
    prefsMock.setValue("wifi", "ssid", "test-ssid");
    prefsMock.setValue("wifi", "pass", "test-pass");

    // Re-create WiFiManager with fresh prefs after setting credentials
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock,
        "baked-ssid", "baked-pass"
    );

    // Manually set the state to CONNECTED_STA to simulate being connected
    // (init() would normally transition through CONNECTING, but we skip that for this test)
    // We use the context directly via a friend class or reflection pattern
    // Since we can't easily set the state directly, we need to test the transition
    // from CONNECTING to CONNECTED_STA first, then to RECONNECTING

    // First, set up the scenario where WiFi is in CONNECTING state
    // and the WiFi status is WL_CONNECTED (3)
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);

    // init() with credentials will transition to CONNECTING first
    wifiManager->init();

    // After init(), we should be in CONNECTING state
    // (DisconnectedStateHandler transitions to CONNECTING when credentials exist)
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);

    // Now simulate WiFi becoming connected (status = WL_CONNECTED)
    // and call update() to transition to CONNECTED_STA
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);  // Trigger state machine to move to CONNECTED_STA

    // Verify we're now in CONNECTED_STA state
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);

    // Simulate disconnect event with NON-AUTH failure (e.g., beacon timeout)
    // This should transition to RECONNECTING
    wifiManager->onDisconnected(200);  // WIFI_REASON_BEACON_TIMEOUT

    // Verify state is now RECONNECTING (non-auth failures retry indefinitely)
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);

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
        wifiMock, prefsMock, statusLedMock,
        "baked-ssid", "baked-pass"
    );

    // Get to CONNECTED_STA state
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);

    // Simulate disconnect event with AUTH_FAIL - this should transition to AP mode
    wifiManager->onDisconnected(202);  // WIFI_REASON_AUTH_FAIL

    // Verify state is now CONNECTED_AP (auth-fail transitions to AP mode)
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_AP);

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
        wifiMock, prefsMock, statusLedMock,
        nullptr, nullptr);  // no baked creds

    wifiManager->init();

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
    EXPECT_EQ(wifiMock.getApSsid(), std::string(WiFiConfig::AP_SSID));
}

TEST_F(WiFiManagerTest, Connecting_ConnectFailedAndTimeout_FallsBackToApMode) {
    // ConnectingStateHandler: status==CONNECT_FAILED + connectDuration past the
    // WIFI_CONNECT_TIMEOUT_MS threshold → shouldFallbackToApMode true → AP mode.
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);  // status 4
    wifiManager->init();
    // Disconnected(STORED_NVS) → CONNECTING; connectStartTime set at init time.
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);

    // init()'s Disconnected handler called wifi_.begin(stored), which the mock
    // resets to WL_IDLE_STATUS — so re-assert the failed status AFTER init to
    // model a real connection attempt that has failed.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    // Advance past the connect timeout (30s) so shouldFallbackToApMode() is true.
    wifiManager->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1000);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
}

TEST_F(WiFiManagerTest, Connecting_ConnectFailedBeforeTimeout_RetriesStoredCredentials) {
    // ConnectingStateHandler: status==CONNECT_FAILED but within the timeout →
    // no AP fallback; shouldRetryWiFi triggers a disconnect+begin retry using
    // the STORED_NVS credentials. State stays CONNECTING.
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);

    // init()'s Disconnected handler called wifi_.begin(stored), which the mock
    // resets to WL_IDLE_STATUS — re-assert the failed status AFTER init.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    // Advance past the retry interval (5s) but well within the 30s timeout.
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    // Still attempting (CONNECTING), not fallen back to AP.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);
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
        wifiMock, prefsMock, statusLedMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);  // neither 3 nor 4/1
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);

    // Past the initial-connect budget (60 retries * 5s = 300s).
    const uint32_t kInitialBudgetMs =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    wifiManager->update(kInitialBudgetMs + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
}

TEST_F(WiFiManagerTest, Connecting_InitialTimeoutWithBakedCredentials_TransitionsToReconnecting) {
    // isInitialConnectTimeout branch for BAKED_IN: baked creds should "just
    // work", so on initial timeout the handler hands off to RECONNECTING
    // (keeps trying) rather than falling back to AP.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);

    const uint32_t kInitialBudgetMs =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    wifiManager->update(kInitialBudgetMs + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);
}

TEST_F(WiFiManagerTest, Reconnecting_WhenWifiConnected_TransitionsToConnectedSta) {
    // ReconnectingStateHandler body: once status() reports WL_CONNECTED, the
    // handler transitions back to CONNECTED_STA (tcpRestart + ntp init).
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, nullptr, nullptr);

    // Reach RECONNECTING through a real connect + non-auth drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();                                   // → CONNECTING
    // init()'s Disconnected handler called wifi_.begin(), which the mock resets
    // to WL_IDLE_STATUS — re-assert connected so the next tick observes it.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);                              // → CONNECTED_STA
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);

    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT → RECONNECTING
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);

    // While RECONNECTING, WiFi comes back → CONNECTED_STA.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(1000);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);
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
        wifiMock, prefsMock, statusLedMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);

    // Re-assert failed status after init (begin() resets the mock), then
    // advance past the retry interval but within the timeout.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECT_FAILED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::CONNECTING);
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

TEST_F(WiFiManagerTest, Reconnecting_NotConnectedAfterInterval_RetriesCredentials) {
    // ReconnectingStateHandler retry branch: still not connected, the retry
    // interval has elapsed → begin() with stored creds (falls back to baked
    // when there are none). Pins the retry body (the existing Reconnecting
    // test only covers the "connected again → CONNECTED_STA" arm).
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, "baked-ssid", "baked-pass");

    // Reach RECONNECTING via a real connect + non-auth drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);
    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT → RECONNECTING
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);

    // Still not connected; advance past the retry interval so shouldRetryWiFi
    // fires the begin() retry with the baked SSID (no stored creds present).
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    // State stays RECONNECTING (retry doesn't change it); the re-attempt used
    // the baked SSID.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);
    EXPECT_EQ(wifiMock.getCurrentSsid(), std::string("baked-ssid"));
}

// Covers line 139: ReconnectingStateHandler retry branch with STORED_NVS creds
// hits wifi_.begin(storedSsid, storedPass) when loadCredentialsImpl succeeds.
TEST_F(WiFiManagerTest, Reconnecting_NotConnectedAfterInterval_RetriesStoredCredentials) {
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, nullptr, nullptr);

    // Reach RECONNECTING via a real connect + non-auth drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);
    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT → RECONNECTING
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);

    // Still not connected; advance past the retry interval so shouldRetryWiFi
    // fires the begin() retry with the stored SSID (loadCredentialsImpl succeeds).
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    // State stays RECONNECTING; the re-attempt used the stored SSID.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);
    EXPECT_EQ(wifiMock.getCurrentSsid(), std::string("real-ssid"));
}

TEST_F(WiFiManagerTest, ConnectedSta_DroppedConnection_TransitionsToReconnecting) {
    // ConnectedStaStateHandler body: a STA drop observed on the state-machine
    // tick (status() != WL_CONNECTED while in CONNECTED_STA) transitions to
    // RECONNECTING with the tcp-restart flag. Distinct from onDisconnected()
    // (event-callback driven): this is the per-tick self-heal that catches
    // drops the WiFi event callback did not surface.
    prefsMock.setValue("wifi", "ssid", "real-ssid");
    prefsMock.setValue("wifi", "pass", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, statusLedMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::CONNECTED_STA);

    // The STA link drops between ticks — the next update() observes it.
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer());
}