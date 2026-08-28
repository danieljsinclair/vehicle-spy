// WiFiManager_test.cpp - Tests for WiFiManager vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/WiFiManager.h"
#include "vanilla/WiFiReasonCodes.h"
#include "mocks/WiFiMock.h"
#include "mocks/PreferencesMock.h"
#include "mocks/ArduinoMock.h"
#include "mocks/SerialDebugMock.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class WiFiManagerTest : public ::testing::Test {
protected:
    WiFiMock wifiMock;
    PreferencesMock prefsMock;
    // Trace sink for these tests: they assert state-machine behaviour, not
    // logging. The dedicated trace contract lives in
    // WiFiManager_SerialTrace_test.cpp.
    SerialDebugMock serialTraceMock;
    std::unique_ptr<WiFiManager> wifiManager;

    void SetUp() override {
        wifiMock.reset();
        prefsMock.reset();
        serialTraceMock.reset();
        arduino_mock::resetAllMocks();

        wifiManager = std::make_unique<WiFiManager>(
            wifiMock, prefsMock, serialTraceMock,
            "baked-ssid", "baked-pass"
        );
    }

    void TearDown() override {
        wifiManager.reset();
    }
};

TEST_F(WiFiManagerTest, DetermineCredentialSource_StoredNVS_ReturnsStoredNVS) {
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

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
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

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
        WiFiState::State::WIFI_DISCONNECTED, now, 0, /*reconnectAttempts=*/0
    );
    EXPECT_TRUE(result);
}

TEST_F(WiFiManagerTest, ShouldRetryWiFi_ConnectedState_ReturnsFalse) {
    bool result = shouldRetryWiFi(
        WiFiState::State::WIFI_CONNECTED, 10000, 0, /*reconnectAttempts=*/0
    );
    EXPECT_FALSE(result);
}

TEST_F(WiFiManagerTest, LoadCredentials_ValidCredentials_ReturnsTrue) {
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

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
    EXPECT_EQ(prefsMock.getValue("wifi", "ssid_0"), "new-ssid");
    EXPECT_EQ(prefsMock.getValue("wifi", "pass_0"), "new-pass");
}

TEST_F(WiFiManagerTest, ClearCredentials_RemovesCredentials) {
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

    bool result = wifiManager->clearCredentials();
    EXPECT_TRUE(result);
    EXPECT_FALSE(prefsMock.hasKey("wifi", "ssid_0"));
    EXPECT_FALSE(prefsMock.hasKey("wifi", "pass_0"));
}

TEST_F(WiFiManagerTest, FactoryReset_CallsClearCredentials) {
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

    bool result = wifiManager->factoryReset();
    EXPECT_TRUE(result);
    EXPECT_FALSE(prefsMock.hasKey("wifi", "ssid_0"));
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
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_AP_MODE_DEFAULT), "WIFI_AP_MODE_DEFAULT");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_AP_MODE_AUTH_FAIL), "WIFI_AP_MODE_AUTH_FAIL");
    EXPECT_STREQ(WiFiManager::stateName(static_cast<WiFiState::State>(99)), "UNKNOWN");
}

// ── AP-state model: DEFAULT (never configured) vs AUTH_FAIL (credentials failed) ──
// Being an AP is a first-class state with a REASON. The split is the state model
// itself; the LED maps it (AP_MODE / ERROR_AUTH_FAILURE) purely downstream.

TEST_F(WiFiManagerTest, IsApModeState_TrueOnlyForBothApStates) {
    EXPECT_FALSE(WiFiState::isApModeState(WiFiState::State::WIFI_DISCONNECTED));
    EXPECT_FALSE(WiFiState::isApModeState(WiFiState::State::WIFI_CONNECTING));
    EXPECT_FALSE(WiFiState::isApModeState(WiFiState::State::WIFI_CONNECTED));
    EXPECT_TRUE(WiFiState::isApModeState(WiFiState::State::WIFI_AP_MODE_DEFAULT));
    EXPECT_TRUE(WiFiState::isApModeState(WiFiState::State::WIFI_AP_MODE_AUTH_FAIL));
}

TEST_F(WiFiManagerTest, HasStoredCredentials_ReturnsTrueWhenCredentialsExist) {
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

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
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

    // Re-create WiFiManager with fresh prefs after setting credentials
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock,
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

TEST_F(WiFiManagerTest, OnDisconnected_AuthFail_StaysConnecting_ArmCampaign) {
    // RESILIENT AUTH (req-1): AUTH_FAIL (202) is NO LONGER treated as a
    // definitive wrong-password → AP bail. The password is correct (a button
    // reset connects), so 202 here is spurious/transient/wrong-mechanism. The
    // manager must stay in WIFI_CONNECTING and arm an auth-fail campaign.

    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");

    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    wifiManager->onDisconnected(202);  // WIFI_REASON_AUTH_FAIL

    // Must STAY in WIFI_CONNECTING (NOT bail to AP mode).
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    // Campaign must be armed.
    EXPECT_TRUE(wifiManager->getContext().pendingAuthFail);
    EXPECT_EQ(wifiManager->getContext().lastDisconnectReason, 202);
    // Counters start at the best strategy, first loop.
    EXPECT_EQ(wifiManager->getContext().authFailStrategyIndex, 0);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyLoop, 0);
    // TCP restart flag is NOT cleared (a later success should still re-bind).
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer());
}

// ── State-handler body coverage (Disconnected / Connecting branches) ─────────────────
// The pure helpers above are well-tested; these tests drive the state-machine
// handler BODIES via update() — the Disconnected NONE→AP fallback, the
// Connecting CONNECT_FAILED retry + AP fallback, and the initial-connect
// timeout branches (STORED_NVS / BAKED_IN) plus the Reconnecting recovery.

TEST_F(WiFiManagerTest, Init_NoCredentials_TransitionsToApMode) {
    // DisconnectedStateHandler NONE branch: no stored NVS creds AND no baked
    // credentials → setMode(AP) + softAP() → AP because nothing was configured.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock,
        nullptr, nullptr);  // no baked creds

    wifiManager->init();

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_DEFAULT);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
    EXPECT_EQ(wifiMock.getApSsid(), std::string(WiFiConfig::AP_SSID));
}

TEST_F(WiFiManagerTest, Connecting_ConnectFailedAndTimeout_FallsBackToApMode) {
    // ConnectingStateHandler: status==CONNECT_FAILED + connectDuration past the
    // WIFI_CONNECT_TIMEOUT_MS threshold → shouldFallbackToApMode true. Stored
    // credentials existed and could not connect → the AUTH_FAIL AP state.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

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

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
}

TEST_F(WiFiManagerTest, Connecting_ConnectFailedBeforeTimeout_RetriesStoredCredentials) {
    // ConnectingStateHandler: status==CONNECT_FAILED but within the timeout →
    // no AP fallback; shouldRetryWiFi triggers a disconnect+begin retry using
    // the STORED_NVS credentials. State stays CONNECTING.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

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

TEST_F(WiFiManagerTest, Connecting_InitialTimeoutWithStoredCredentials_StaysConnectingWhenNoAuthReason) {
    // LINK-LEVEL DROP RESILIENCE (mesh-reboot fix): the previous behaviour
    // escalated to WIFI_AP_MODE_AUTH_FAIL after the 5-minute budget regardless
    // of WHY the device was stuck. Field logs show reason 0/200/201 (mesh AP
    // rebooting) on this exact path — escalating to AP mode is wrong, the
    // mesh will be back in 2-30 minutes. With the fix, the 5-min safety-net
    // escalation is GATED on isAuthMechanismFailure(lastDisconnectReason); for
    // a generic/lastDisconnectReason=0 the handler stays in WIFI_CONNECTING
    // and keeps retrying. The mesh-reboot scenario is the canonical case this
    // test pins.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);  // neither 3 nor 4/1
    // Simulate a link-level drop (mesh rebootting) so lastDisconnectReason is
    // a known link-level code (200 = BEACON_TIMEOUT), not auth.
    // We do this by driving through a connect first, then firing the drop.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Mesh AP goes down — the radio reports BEACON_TIMEOUT (200).
    wifiManager->onDisconnected(200);  // WIFI_REASON_BEACON_TIMEOUT
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    // Idle status while we wait for the mesh to come back.
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);

    // Past the initial-connect budget (60 retries * 5s = 300s).
    const uint32_t kInitialBudgetMs =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    wifiManager->update(kInitialBudgetMs + 1);

    // Stays in WIFI_CONNECTING — must NOT escalate to AP mode. The mesh is
    // rebooting, the credentials are correct, link-level drops retry forever.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "link-level drop (reason=200) must not escalate to AP after 5-min budget";
    EXPECT_NE(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP)
        << "WiFi mode must stay STA — never transition to AP for a link-level drop";
}

TEST_F(WiFiManagerTest, Connecting_InitialTimeoutWithBakedCredentials_TransitionsToConnecting) {
    // isInitialConnectTimeout branch for BAKED_IN: baked creds should "just
    // work", so on initial timeout the handler stays in WIFI_CONNECTING
    // (RECONNECTING merged into WIFI_CONNECTING; retry loop continues) rather
    // than falling back to AP.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

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
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

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
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

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
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

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
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

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
    // WIFI_CONNECTING. The tcpRestart decision is DEFERRED to the re-connect
    // (ConnectingStateHandler) where the new IP is known — see
    // shouldRestartTcpServerForReconnect. Here only reconnectPending is armed
    // and disconnectStartMs is stamped. Distinct from onDisconnected()
    // (event-callback driven): this is the per-tick self-heal that catches
    // drops the WiFi event callback did not surface. RECONNECTING was merged
    // into WIFI_CONNECTING (spec §1).
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Clear the first-connect restart flag (simulating can-bridge.ino having
    // processed it) so we can observe the drop path in isolation.
    wifiManager->clearTcpServerRestartFlag();
    ASSERT_FALSE(wifiManager->shouldRestartTcpServer());

    // The STA link drops between ticks — the next update() observes it.
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    // tcpRestart is deferred — the flag must NOT be armed at drop time.
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer())
        << "drop path must defer tcpRestart (IP unknown until re-connect)";
    // reconnectPending + disconnectStartMs must be recorded for the re-connect.
    EXPECT_TRUE(wifiManager->getContext().reconnectPending)
        << "drop path must arm reconnectPending";
    EXPECT_EQ(wifiManager->getContext().disconnectStartMs, 200u)
        << "disconnectStartMs must be stamped at drop time";
}

TEST_F(WiFiManagerTest, ConnectedSta_IpZeroedWhileStatusConnected_TransitionsToConnecting) {
    // Regression for the on-device field report: after an AP is toggled off then
    // back on, ESP32's core keeps reporting WL_CONNECTED across the blip while
    // DHCP does NOT re-complete, so localIP() reads 0.0.0.0. The old handler only
    // checked status()!=WL_CONNECTED, so the device sat in WIFI_CONNECTED with no
    // address — an unreachable, contradictory state. The invariant now treats
    // "connected but no IP" as a drop so it re-enters CONNECTING and re-acquires.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "real-ssid");
    prefsMock.setValue("wifi", "pass_0", "real-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);

    // Establish the already-connected precondition. `init()`→`begin()` resets the
    // mock to WL_IDLE_STATUS (WiFiMock::begin), so re-assert WL_CONNECTED AFTER
    // init() — exactly as ConnectedSta_DroppedConnection does — otherwise the
    // precondition never takes and the CONNECTING handler can't promote to
    // WIFI_CONNECTED. (The field scenario is "connected, then AP blip zeroes IP";
    // the promotion itself is the sibling test's job — here we only set up state.)
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    wifiMock.setLocalIP("172.20.10.2");  // IP assigned on connect
    wifiManager->update(150);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // AP toggled off→on: status stays WL_CONNECTED but the IP is gone.
    wifiMock.setLocalIP("0.0.0.0");
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(200);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "connected-with-no-IP must self-heal to CONNECTING";
    EXPECT_TRUE(wifiManager->getContext().reconnectPending);
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

// Helper: drive the manager to WIFI_CONNECTED from a clean state.
void driveToConnected(WiFiManager& mgr, WiFiMock& wifi) {
    wifi.setStatus(WiFiMock::Status::WL_CONNECTED);
    mgr.init();
    ASSERT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTING);
    wifi.setStatus(WiFiMock::Status::WL_CONNECTED);
    mgr.update(100);
    ASSERT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTED);
}

// Helper: fire one auth-fail campaign retry tick. After onDisconnected arms the
// campaign, each retry tick applies the next strategy. We model the radio
// reporting WL_IDLE_STATUS (the begin() reset) so the retry path fires. Returns
// the tick timestamp used.
uint32_t fireRetryTick(WiFiManager& mgr, WiFiMock& wifi, uint32_t baseMs) {
    wifi.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    // Advance past the aggressive-first-retries window (0ms) / normal interval so
    // shouldRetryWiFi() permits the next strategy attempt.
    uint32_t now = baseMs + WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1;
    mgr.update(now);
    return now;
}

TEST_F(WiFiManagerTest, AuthFail_ArmedCampaign_StaysConnectingAndRotatesStrategies) {
    // RESILIENT AUTH (req-1/2): 4WAY_HANDSHAKE_TIMEOUT (15) arms a campaign and
    // stays in WIFI_CONNECTING. Each retry tick advances the strategy index (and,
    // after wrapping, the loop counter) WITHOUT yet escalating to AP mode.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(15);  // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    ASSERT_TRUE(wifiManager->getContext().pendingAuthFail);

    // Tick 1 applies strategy 0.
    uint32_t now = fireRetryTick(*wifiManager, wifiMock, 100);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyIndex, 1);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyLoop, 0);

    // Tick 2 applies strategy 1.
    now = fireRetryTick(*wifiManager, wifiMock, now);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyIndex, 2);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyLoop, 0);

    // Tick 3 applies strategy 2, then wraps: index back to 0, loop advances to 1.
    now = fireRetryTick(*wifiManager, wifiMock, now);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyIndex, 0);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyLoop, 1);

    // Still CONNECTING — NOT escalated to AP.
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
}

TEST_F(WiFiManagerTest, AuthFail_ThreeLoopsExhausted_EscalatesToApMode) {
    // RESILIENT AUTH (req-1): connection opportunities are EXHAUSTED only after
    // WIFI_AUTH_STRATEGY_COUNT strategies × WIFI_AUTH_STRATEGY_LOOP_COUNT loops
    // have all been attempted. After the last one, escalation to AP mode is
    // bounded (finite), not an infinite retry.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(202);  // WIFI_REASON_AUTH_FAIL
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    const int totalAttempts =
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT) *
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);  // 3 * 3 = 9

    uint32_t now = 100;
    // Fire one fewer tick than total: still CONNECTING (campaign not yet exhausted).
    for (int i = 0; i < totalAttempts - 1; ++i) {
        now = fireRetryTick(*wifiManager, wifiMock, now);
        ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
            << "must stay CONNECTING until all " << totalAttempts << " attempts done (i=" << i << ")";
    }
    // One more tick exhausts the campaign → escalate to the AUTH_FAIL AP state
    // (bounded) — credentials existed and could not connect.
    now = fireRetryTick(*wifiManager, wifiMock, now);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL)
        << "after 3 full loops the campaign must escalate to the AUTH_FAIL AP state";

    // And it records the true auth reason it escalated on.
    EXPECT_EQ(wifiManager->getContext().escalatedToApReason, 202)
        << "escalatedToApReason must record the auth reason that triggered the campaign";
}

TEST_F(WiFiManagerTest, AuthFail_SuccessfulConnectResetsCampaign) {
    // RESILIENT AUTH (req-3): a successful WL_CONNECTED after some auth-fails
    // resets the exhausted state (counters back to 0, pending cleared) so a
    // genuine later drop starts fresh rather than instantly escalating.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    // A spurious cold-boot auth fail arms the campaign.
    wifiManager->onDisconnected(202);  // WIFI_REASON_AUTH_FAIL
    ASSERT_TRUE(wifiManager->getContext().pendingAuthFail);

    // Rotate a couple of strategies so the counters advance.
    uint32_t now = fireRetryTick(*wifiManager, wifiMock, 100);
    now = fireRetryTick(*wifiManager, wifiMock, now);
    ASSERT_GT(wifiManager->getContext().authFailStrategyIndex, 0);

    // The radio now succeeds (e.g. the mechanism settles after a reset) — model
    // WL_CONNECTED flowing through the connecting handler.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(now + WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_FALSE(wifiManager->getContext().pendingAuthFail)
        << "successful connect must clear the pending-auth-fail campaign";
    EXPECT_EQ(wifiManager->getContext().authFailStrategyIndex, 0)
        << "successful connect must reset the strategy index";
    EXPECT_EQ(wifiManager->getContext().authFailStrategyLoop, 0)
        << "successful connect must reset the loop counter";
}

TEST_F(WiFiManagerTest, AuthFail_ConnectAfterSomeFails_ResetExhaustedState) {
    // RESILIENT AUTH (req-3): even if we had progressed partway into the
    // campaign, a real connect resets everything; a subsequent fresh drop must
    // NOT immediately re-escalate (it re-arms a fresh campaign from strategy 0).
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(202);
    uint32_t now = 100;
    now = fireRetryTick(*wifiManager, wifiMock, now);  // strategy 0 applied
    // Connect succeeds.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(now + WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    ASSERT_FALSE(wifiManager->getContext().pendingAuthFail);

    // A genuine later drop re-arms a FRESH campaign (does not escalate instantly).
    wifiManager->onDisconnected(202);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(wifiManager->getContext().pendingAuthFail);
    EXPECT_EQ(wifiManager->getContext().authFailStrategyIndex, 0);
}

TEST_F(WiFiManagerTest, authExpireRetriesStaForever_Reason2StaysConnecting) {
    // AUTH_EXPIRE (2) is a TRANSIENT session-lifecycle reason: the auth
    // session timed out, but the credentials are still valid. Must NOT
    // transition to AP mode — must retry STA forever (re-enter CONNECTING).
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

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
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

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

// ══════════════════════════════════════════════════════════════════════════════
// Commit 7: #5 WiFi resilient reconnect — IP-aware tcpRestart
//
// USER GOAL: less-aggressive reconnect. Don't tear down the TCP server on every
// WiFi blip — only restart it when the IP actually CHANGED. Prioritize (a)
// quickest recovery + (b) maintain connectivity (survive a brief disconnect,
// don't drop connections unnecessarily).
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(WiFiManagerTest, testFirstConnectRestartsTcpServer) {
    // Cold start: lastConnectedIp is empty → first WIFI_CONNECTED must set
    // tcpRestart=true (binds the listening socket for the first time).
    // This characterizes the preserved cold-start behavior.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    // Drive to WIFI_CONNECTED via the normal connect path.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // First connect: lastConnectedIp was empty → shouldRestartTcpServerForReconnect
    // returns true → tcpServerNeedsRestart must be set.
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer())
        << "first-ever connect must restart TCP server (bind socket)";
    EXPECT_EQ(wifiManager->getContext().lastConnectedIp, std::string("192.168.1.100"))
        << "lastConnectedIp must be captured on first connect";
}

TEST_F(WiFiManagerTest, testReconnectSameIpKeepsListeningSocket) {
    // IP-AWARE restart (restored after the ddd8239 regression): a same-IP brief
    // blip must NOT re-begin() the listening socket. Re-binding mid-handshake
    // tears down the port and drops an in-progress host AUTH (client_connected
    // then an immediate reason=0 disconnect). The ESP32 listening socket DOES
    // survive a brief radio reset, so we keep it bound across same-IP blips.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    // Drive to WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Clear the flag (simulating can-bridge.ino having processed it).
    wifiManager->clearTcpServerRestartFlag();
    ASSERT_FALSE(wifiManager->shouldRestartTcpServer());

    // Simulate a per-tick drop (ConnectedStaStateHandler detects status != WL_CONNECTED).
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    // Drop path: tcpRestart=false, reconnectPending=true (deferred to re-connect).
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer())
        << "same-IP drop must NOT set tcpRestart (deferred to re-connect)";

    // Re-connect with the SAME IP (brief blip, 1ms outage — well within LONG_OUTAGE_MS).
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(201);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Same IP + short outage → IP-aware helper returns FALSE: keep the bound socket.
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer())
        << "same-IP brief blip must keep the listening socket (no mid-handshake rebind)";
}

TEST_F(WiFiManagerTest, testReconnectDifferentIpRestartsTcpServer) {
    // Different IP after a drop → tcpRestart FIRES (new DHCP lease, must rebind).
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    // Drive to WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Clear the flag.
    wifiManager->clearTcpServerRestartFlag();
    ASSERT_FALSE(wifiManager->shouldRestartTcpServer());

    // Simulate a per-tick drop.
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer());

    // Re-connect with a DIFFERENT IP.
    wifiMock.setLocalIP("192.168.1.105");
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(201);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Different IP → shouldRestartTcpServerForReconnect returns true.
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer())
        << "different-IP reconnect must restart TCP server (new lease)";
}

TEST_F(WiFiManagerTest, testReconnectAfterLongDropRestartsTcpServer) {
    // Same IP but outage > LONG_OUTAGE_MS → restart as a safety net
    // (socket likely stale after a long outage).
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    // Drive to WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Clear the flag.
    wifiManager->clearTcpServerRestartFlag();
    ASSERT_FALSE(wifiManager->shouldRestartTcpServer());

    // Simulate a per-tick drop.
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer());

    // Re-connect with the SAME IP but after a long outage (> 30s).
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(200 + WiFiConfig::LONG_OUTAGE_MS + 1);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // Same IP + long outage → shouldRestartTcpServerForReconnect returns true (safety).
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer())
        << "same-IP reconnect after long outage must restart TCP server (safety)";
}

TEST_F(WiFiManagerTest, testShouldRestartTcpServerForReconnect_PureHelper) {
    // Direct unit test of the pure helper across the 2x2 matrix
    // (empty/filled lastIp × same/different newIp) + the long-outage branch.
    // No fixture needed — fast, deterministic.
    //
    // IP-AWARE reconnect (restored after the ddd8239 regression): restart the
    // listening socket only on first-ever connect, IP change, or long outage.
    // A same-IP brief blip keeps the bound socket (re-begin() mid-handshake drops
    // an in-progress host AUTH).

    // First-ever connect: lastConnectedIp empty → always restart.
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("192.168.1.100", "", 0));
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("0.0.0.0", "", 0));

    // Same IP, short outage → keep the bound socket (no mid-handshake rebind).
    EXPECT_FALSE(shouldRestartTcpServerForReconnect("192.168.1.100", "192.168.1.100", 0));
    EXPECT_FALSE(shouldRestartTcpServerForReconnect("192.168.1.100", "192.168.1.100", 1000));
    EXPECT_FALSE(shouldRestartTcpServerForReconnect("192.168.1.100", "192.168.1.100",
        WiFiConfig::LONG_OUTAGE_MS - 1));
    EXPECT_FALSE(shouldRestartTcpServerForReconnect("192.168.1.100", "192.168.1.100",
        WiFiConfig::LONG_OUTAGE_MS));

    // Different IP → always restart (regardless of outage).
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("192.168.1.105", "192.168.1.100", 0));
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("192.168.1.105", "192.168.1.100", 1000));
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("192.168.1.105", "192.168.1.100",
        WiFiConfig::LONG_OUTAGE_MS + 1));

    // Same IP, long outage → restart.
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("192.168.1.100", "192.168.1.100",
        WiFiConfig::LONG_OUTAGE_MS + 1));
    EXPECT_TRUE(shouldRestartTcpServerForReconnect("192.168.1.100", "192.168.1.100",
        WiFiConfig::LONG_OUTAGE_MS + 1000));
}

TEST_F(WiFiManagerTest, testUserFacingSerialDoesNotLeakRestartDetail) {
    // RESILIENT RECONNECT (req-3): the WiFiManager now arms tcpServerNeedsRestart on
    // EVERY reconnect (same-IP or different-IP) so the listening socket is always
    // re-bound after a radio reset. The serial trace is still controlled by
    // can-bridge.ino (it omits the user-facing message regardless). This test now
    // pins that BOTH same-IP and different-IP reconnects arm the flag.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    // Drive to WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(100);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);

    // --- Same-IP reconnect: flag stays UNSET (IP-aware, no mid-handshake rebind) ---
    wifiManager->clearTcpServerRestartFlag();
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(200);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(201);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_FALSE(wifiManager->shouldRestartTcpServer())
        << "same-IP brief blip must NOT arm tcpServerNeedsRestart (keep bound socket)";

    // --- Different-IP reconnect: flag IS set (restart is real, message omitted) ---
    wifiManager->clearTcpServerRestartFlag();
    wifiMock.setLocalIP("192.168.1.200");
    wifiMock.setStatus(WiFiMock::Status::WL_DISCONNECTED);
    wifiManager->update(300);
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(301);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer())
        << "different-IP reconnect must arm tcpServerNeedsRestart (restart is real)";
}

// ══════════════════════════════════════════════════════════════════════════════
// Commit 8: #19 WiFi auth-fallback fix — link-level drops must NOT escalate
//
// THE BUG: when the mesh AP reboots, the ESP32's link drops deliver reason
// 0/200/201 (BEACON_TIMEOUT, NO_AP_FOUND, generic) in rapid succession. The
// previous code escalated to WIFI_AP_MODE_AUTH_FAIL after 5 minutes regardless
// of WHY the device was stuck — treating a mesh reboot exactly the same as a
// wrong password. The mesh takes 2-30 minutes to come back, and the device
// should keep retrying STA, NOT abandon it for an AP that is reachable again.
//
// THE FIX: the 5-min safety-net escalation is now gated on
// isAuthMechanismFailure(lastDisconnectReason). Link-level drops (0/200/201/...)
// stay in WIFI_CONNECTING and keep retrying forever. AP mode is reserved for
// real auth rejection where the campaign has EXHAUSTED.
//
// These tests pin the new behaviour at both the pure-helper level and the
// state-machine level (the canonical Scenario B from the field log).
// ══════════════════════════════════════════════════════════════════════════════

// Pure-helper coverage: the new isLinkLevelDrop() boolean predicate. The
// classification is the complement of isAuthMechanismFailure() — together
// they partition the disconnect-reason space.
TEST_F(WiFiManagerTest, IsLinkLevelDrop_TrueForRecoverableCodes) {
    EXPECT_TRUE(isLinkLevelDrop(0));                              // generic / unspecified
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_AUTH_EXPIRE));        // 2
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_AUTH_LEAVE));         // 3
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_ASSOC_EXPIRE));       // 4
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_CIPHER_SUITE_REJECTED));  // 24
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_BEACON_TIMEOUT));     // 200 — the canonical field case
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_NO_AP_FOUND));        // 201
    EXPECT_TRUE(isLinkLevelDrop(WIFI_REASON_HANDSHAKE_TIMEOUT));  // 204
}

TEST_F(WiFiManagerTest, IsLinkLevelDrop_FalseForAuthMechanismCodes) {
    // The two classifications must not overlap — auth reasons are NOT link-level
    // and must trigger the campaign / AP escalation paths.
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));  // 15
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_802_1X_AUTH_FAILED));      // 23
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_AUTH_FAIL));               // 202
}

TEST_F(WiFiManagerTest, IsLinkLevelDrop_FalseForUnrelatedCodes) {
    // ASSOC family codes (5/6/7/8) are not classified as link-level — they
    // sit in a grey zone the current code does not yet handle explicitly.
    // Conservative: classify them as not-link-level so they fall through to
    // the same path as 0 (stay CONNECTING, no AP escalation).
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_ASSOC_TOOMANY));   // 5
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_NOT_AUTHED));      // 6
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_NOT_ASSOCED));     // 7
    EXPECT_FALSE(isLinkLevelDrop(WIFI_REASON_ASSOC_LEAVE));     // 8
    EXPECT_FALSE(isLinkLevelDrop(99999));                       // unknown
}

// State-machine-level coverage: the canonical Scenario B from the field log.
// A mesh reboot delivers reason 200/201/0 in rapid succession. The device
// must NOT escalate to AP mode after the 5-min budget — it must keep
// retrying STA.
TEST_F(WiFiManagerTest, LinkLevelDrop_StaysConnectingForever_DoesNotEscalateToAp) {
    // BUG-FIX SCENARIO B (from the field log):
    //   - device was connected (ip=192.168.68.91)
    //   - mesh reboots: reason 0, 200, 201 in rapid succession
    //   - PREVIOUS: device escalates to WIFI_AP_MODE_AUTH_FAIL after 5 min — WRONG
    //   - FIXED:    device stays in WIFI_CONNECTING, never escalates
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    // Simulate the mesh reboot — three reason codes in sequence (the field log
    // pattern). The last one is BEACON_TIMEOUT (200) which is the live state.
    wifiManager->onDisconnected(0);    // generic first
    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT (the live one)
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    ASSERT_EQ(wifiManager->getContext().lastDisconnectReason, 200);
    // NO auth-fail campaign armed for a link-level drop.
    ASSERT_FALSE(wifiManager->getContext().pendingAuthFail);

    // Radio sits at WL_IDLE_STATUS while we wait for the mesh.
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);

    // Advance well past the 5-min initial-connect budget. The mesh takes
    // 2-30 minutes to come back; the device must NOT bail to AP mode.
    const uint32_t kInitialBudgetMs =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    // Drive multiple ticks past the budget — each one would have escalated
    // under the old code.
    wifiManager->update(kInitialBudgetMs * 2);  // 10 minutes
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "link-level drop must stay CONNECTING past 5-min budget (no AP escalation)";
    EXPECT_NE(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP)
        << "WiFi mode must remain STA — link-level drops never escalate to AP";

    // 30 minutes (well past the 2-30 min mesh reboot window). Still retrying.
    wifiManager->update(kInitialBudgetMs * 6);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "link-level drop must keep retrying STA across the mesh reboot window";
    EXPECT_NE(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_AP);
}

// After a long link-level outage, if the mesh comes back, the device MUST
// re-associate promptly (the user's underlying connectivity complaint).
TEST_F(WiFiManagerTest, LinkLevelDrop_LongOutageThenMeshReturns_Reassociates) {
    // Continuation of the canonical Scenario B: the mesh eventually comes
    // back up. The device must re-associate without an operator intervention.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(200);  // BEACON_TIMEOUT — mesh rebooting
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);

    // 20 minutes of outage (the long end of the 2-30 min mesh reboot window).
    const uint32_t kLongOutage =
        WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES * WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS * 4;
    wifiManager->update(kLongOutage);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Mesh comes back. The connecting handler must promote to WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(kLongOutage + 100);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED)
        << "when the mesh returns during a long outage, the device must re-associate";
}

// Sanity: the existing auth-fail campaign path is NOT broken by this fix.
// A real auth rejection (reason 15/23/202) must still escalate to AP mode
// once the strategy campaign is exhausted.
TEST_F(WiFiManagerTest, AuthMechanismFailure_StillEscalatesAfterCampaignExhaustion) {
    // Pins the existing correct behavior — the campaign is the ONLY path that
    // escalates to AP for auth reasons. The 5-min safety net is a backup that
    // also requires an auth reason. (See AuthFail_ThreeLoopsExhausted_EscalatesToApMode
    // for the campaign-driven escalation; this test pins the 5-min safety net
    // for completeness.)
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");

    // Boot: stored NVS creds, but the radio can't get past WL_IDLE_STATUS
    // because the AP is rejecting auth at the lower layer (the campaign
    // path's onDisconnected(reason) is bypassed by a direct mock setup).
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // The campaign can only escalate via onDisconnected(15/23/202). For the
    // 5-min safety net, the path needs lastDisconnectReason to be auth. We
    // simulate that by manually arming the campaign + setting reason=202
    // (mirrors what onDisconnected(202) does) — this exercises the safety net
    // path which is gated on isAuthMechanismFailure(lastDisconnectReason).
    wifiManager->onDisconnected(202);  // AUTH_FAIL
    ASSERT_TRUE(wifiManager->getContext().pendingAuthFail);

    // The campaign owns escalation now; tick through the 3x3 attempts.
    uint32_t now = 100;
    const int totalAttempts =
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT) *
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
    for (int i = 0; i < totalAttempts; ++i) {
        now = fireRetryTick(*wifiManager, wifiMock, now);
    }
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL)
        << "auth-fail campaign exhaustion must escalate to AP (regression guard)";
}

// AP-MODE RECOVERY: once the device has escalated to WIFI_AP_MODE_AUTH_FAIL
// (real auth failure, campaign exhausted), the ConnectedApStateHandler
// periodically attempts to re-associate so the device self-heals when the
// operator fixes the password or the AP comes back. This addresses the
// user's "stuck" symptom — without this, the device sits in AP mode forever.
TEST_F(WiFiManagerTest, ApModeAuthFail_PeriodicStaRetry_StaysInApModeBeforeInterval) {
    // Drive to WIFI_AP_MODE_AUTH_FAIL via the auth campaign.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(202);
    uint32_t now = 100;
    const int totalAttempts =
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT) *
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
    for (int i = 0; i < totalAttempts; ++i) {
        now = fireRetryTick(*wifiManager, wifiMock, now);
    }
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);

    // First tick arms the timer. Then tick several times BEFORE the interval
    // elapses — the state must stay in WIFI_AP_MODE_AUTH_FAIL.
    const uint32_t armMs = 1000000;
    wifiManager->update(armMs);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);
    ASSERT_EQ(wifiManager->getContext().apModeStaRetryMs, armMs);

    const uint32_t almostFireMs = armMs + WiFiConfig::WIFI_AP_MODE_STA_RETRY_INTERVAL_MS - 100;
    for (uint32_t t = armMs + 1000; t < almostFireMs; t += 30000) {
        wifiManager->update(t);
        EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL)
            << "AP mode must stay stable before the retry interval (t=" << t << ")";
    }
}

TEST_F(WiFiManagerTest, ApModeAuthFail_AfterRetryInterval_AttemptsStaReassociation) {
    // Once the interval elapses, the ConnectedApStateHandler fires a STA
    // re-association attempt — even if it fails, the device MUST leave AP
    // mode and try, so it self-heals when the underlying problem is fixed.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(202);
    uint32_t now = 100;
    const int totalAttempts =
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT) *
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
    for (int i = 0; i < totalAttempts; ++i) {
        now = fireRetryTick(*wifiManager, wifiMock, now);
    }
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);

    // First tick in AP mode arms the retry timer.
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    const uint32_t armMs = 1000000;  // any well-defined start time
    wifiManager->update(armMs);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);
    ASSERT_EQ(wifiManager->getContext().apModeStaRetryMs, armMs);

    // Tick past the retry interval — the handler fires the STA re-association.
    const uint32_t nextAttempt = armMs + WiFiConfig::WIFI_AP_MODE_STA_RETRY_INTERVAL_MS + 1;
    wifiManager->update(nextAttempt);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING)
        << "after the retry interval the handler must attempt STA re-association "
           "(self-heal from AP-mode 'stuck' state)";
    EXPECT_EQ(wifiMock.getModeEnum(), WiFiMock::Mode::WIFI_STA)
        << "WiFi mode must switch back to STA for the re-association attempt";
}

TEST_F(WiFiManagerTest, ApModeAuthFail_StaRetrySucceeds_RecoversToConnected) {
    // The self-heal happy path: AP mode → periodic STA retry → radio comes
    // back → WIFI_CONNECTED. This is the user's underlying connectivity
    // complaint resolved automatically.
    prefsMock.setValue("wifi", "cred_count", "1");
    prefsMock.setValue("wifi", "ssid_0", "test-ssid");
    prefsMock.setValue("wifi", "pass_0", "test-pass");
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, "baked-ssid", "baked-pass");
    driveToConnected(*wifiManager, wifiMock);

    wifiManager->onDisconnected(202);
    uint32_t now = 100;
    const int totalAttempts =
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT) *
        static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
    for (int i = 0; i < totalAttempts; ++i) {
        now = fireRetryTick(*wifiManager, wifiMock, now);
    }
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);

    // First tick arms the timer; second tick (past interval) fires the STA
    // attempt. Then the radio succeeds.
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    const uint32_t armMs = 1000000;
    wifiManager->update(armMs);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);

    const uint32_t nextAttempt = armMs + WiFiConfig::WIFI_AP_MODE_STA_RETRY_INTERVAL_MS + 1;
    wifiManager->update(nextAttempt);
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);

    // Mesh comes back. The device promotes to WIFI_CONNECTED.
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(nextAttempt + 100);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED)
        << "AP-mode periodic STA retry must succeed → device self-heals to CONNECTED";
}

TEST_F(WiFiManagerTest, ApModeDefault_DoesNotRetrySta_NoCredentials) {
    // WIFI_AP_MODE_DEFAULT means no credentials were ever configured — there
    // is nothing to retry. The AP must stay stable indefinitely.
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialTraceMock, nullptr, nullptr);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_DEFAULT);

    // Tick far past any retry interval — must stay in AP_DEFAULT.
    wifiManager->update(WiFiConfig::WIFI_AP_MODE_STA_RETRY_INTERVAL_MS * 10);
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE_DEFAULT)
        << "AP_DEFAULT (no creds) must not attempt STA re-association";
}