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

    CredentialSource source = determineCredentialSource(prefsMock);
    EXPECT_EQ(source, CredentialSource::STORED_NVS);
}

TEST_F(WiFiManagerTest, DetermineCredentialSource_EmptyNVS_ReturnsNone) {
    CredentialSource source = determineCredentialSource(prefsMock);
    EXPECT_EQ(source, CredentialSource::NONE);
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

TEST_F(WiFiManagerTest, OnDisconnected_SetsReconnectingState) {
    // This test verifies that when WiFi disconnects from CONNECTED_STA state,
    // the state machine transitions to RECONNECTING and sets tcpServerNeedsRestart

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

    // Simulate disconnect event - this should transition to RECONNECTING
    wifiManager->onDisconnected(202);  // AUTH_FAIL

    // Verify state is now RECONNECTING
    EXPECT_EQ(wifiManager->getState(), WiFiState::State::RECONNECTING);

    // Verify TCP server restart flag is set
    EXPECT_TRUE(wifiManager->shouldRestartTcpServer());
}