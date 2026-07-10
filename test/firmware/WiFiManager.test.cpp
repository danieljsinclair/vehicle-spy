#include <gtest/gtest.h>
#include "firmware/vanilla/WiFiManager.h"
#include "firmware/vanilla/WiFiReasonCodes.h"

#include <cstring>
#include <string>
#include <vector>

using namespace esp32_firmware;

namespace {

// ── Fakes ──────────────────────────────────────────────────────────────────────
class FakeWiFi : public IWiFi {
public:
    int mode = 0;
    std::string lastSsid, lastPass;
    int beginCalls = 0;
    int statusVal = 0;
    int lastReason = 0;
    std::string localIpVal = "192.168.4.1";
    std::vector<int> events;

    void setMode(int m) override { mode = m; }
    void begin(const char* ssid, const char* pass) override {
        lastSsid = ssid; lastPass = pass; ++beginCalls;
    }
    void disconnect(bool, bool) override {}
    int status() const override { return statusVal; }
    std::string localIP() const override { return localIpVal; }
    std::string softAPIP() const override { return "192.168.4.1"; }
    void softAP(const char* ssid, const char* pass) override {
        lastSsid = ssid; lastPass = pass; mode = 2;
    }
    void setHostname(const char*) override {}
    int getMode() const override { return mode; }
    std::string SSID() const override { return lastSsid; }
    const char* disconnectReasonName(int reason) const override { return ""; }
    void onEvent(std::function<void(int, void*)> cb, int event) override {
        events.push_back(event); (void)cb;
    }
};

// In-memory Preferences replacement.
class FakePreferences : public IPreferences {
public:
    std::string ssid, pass;
    bool cleared = false;

    void begin(const char*, bool) override {}
    void end() override {}
    size_t getBytesLength(const char* key) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) return ssid.size();
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) return pass.size();
        return 0;
    }
    std::string getString(const char* key, const std::string&) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) return ssid;
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) return pass;
        return "";
    }
    size_t putString(const char* key, const std::string& value) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) ssid = value;
        else if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) pass = value;
        return value.size();
    }
    void clear() override { ssid.clear(); pass.clear(); cleared = true; }
};

class FakeStatusLed : public IStatusLED {
public:
    int lastPattern = -1;
    int updateCalls = 0;
    void setPattern(int p) override { lastPattern = p; }
    void update(uint32_t) override { ++updateCalls; }
};

// ── Pure-function tests ────────────────────────────────────────────────────────

TEST(WiFiPureTest, DetermineCredentialSourceStoredNvs) {
    FakePreferences prefs;
    prefs.ssid = "net"; prefs.pass = "pw";
    EXPECT_EQ(determineCredentialSource(prefs, nullptr, nullptr), CredentialSource::STORED_NVS);
}

TEST(WiFiPureTest, DetermineCredentialSourceBakedIn) {
    FakePreferences prefs;  // empty
    EXPECT_EQ(determineCredentialSource(prefs, "baked", "bakedpw"), CredentialSource::BAKED_IN);
}

TEST(WiFiPureTest, DetermineCredentialSourceNone) {
    FakePreferences prefs;  // empty
    EXPECT_EQ(determineCredentialSource(prefs, nullptr, nullptr), CredentialSource::NONE);
}

TEST(WiFiPureTest, DetermineCredentialSourceEmptyBakedTreatedAsNone) {
    FakePreferences prefs;  // empty
    // Baked SSID present but password null -> not BAKED_IN
    EXPECT_EQ(determineCredentialSource(prefs, "baked", nullptr), CredentialSource::NONE);
}

TEST(WiFiPureTest, ShouldFallbackToApModeOnlyStoredAndTimeout) {
    // Stored creds + connect duration over timeout -> fallback.
    EXPECT_TRUE(shouldFallbackToApMode(CredentialSource::STORED_NVS,
                                       WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1));
    // Under timeout -> no fallback.
    EXPECT_FALSE(shouldFallbackToApMode(CredentialSource::STORED_NVS, 1));
    // Baked-in (even over timeout) -> no fallback (keep retrying).
    EXPECT_FALSE(shouldFallbackToApMode(CredentialSource::BAKED_IN,
                                        WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1));
}

TEST(WiFiPureTest, IsInitialConnectTimeout) {
    uint32_t threshold = WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;  // 300000 ms
    EXPECT_FALSE(isInitialConnectTimeout(threshold - 1));
    EXPECT_TRUE(isInitialConnectTimeout(threshold + 1));
}

TEST(WiFiPureTest, ShouldRetryWiFiOnlyForTransientStates) {
    // Connected states never retry.
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::CONNECTED_STA, 10000, 0));
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::CONNECTED_AP, 10000, 0));
    // Transient states retry after interval.
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::DISCONNECTED, 1000, 0));  // < 5000
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::DISCONNECTED, 6000, 0));    // >= 5000
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::CONNECTING, 6000, 0));
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::RECONNECTING, 6000, 0));
}

TEST(WiFiPureTest, LoadCredentialsImplReturnsStored) {
    FakePreferences prefs;
    prefs.ssid = "ssidX"; prefs.pass = "passY";
    std::string ssid, pass;
    EXPECT_TRUE(loadCredentialsImpl(prefs, ssid, pass));
    EXPECT_EQ(ssid, "ssidX");
    EXPECT_EQ(pass, "passY");
}

TEST(WiFiPureTest, LoadCredentialsImplReturnsFalseWhenEmpty) {
    FakePreferences prefs;  // empty
    std::string ssid, pass;
    EXPECT_FALSE(loadCredentialsImpl(prefs, ssid, pass));
}

// ── Public credential API ──────────────────────────────────────────────────────

TEST(WiFiCredentialApiTest, StoreThenHasThenLoad) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);

    EXPECT_FALSE(mgr.hasStoredCredentials());
    EXPECT_TRUE(mgr.storeCredentials("MyNet", "secret"));
    EXPECT_TRUE(mgr.hasStoredCredentials());

    std::string ssid, pass;
    EXPECT_TRUE(mgr.loadCredentials(ssid, pass));
    EXPECT_EQ(ssid, "MyNet");
    EXPECT_EQ(pass, "secret");
}

TEST(WiFiCredentialApiTest, ClearCredentialsRemovesStored) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);

    mgr.storeCredentials("net", "pw");
    ASSERT_TRUE(mgr.hasStoredCredentials());
    EXPECT_TRUE(mgr.clearCredentials());
    EXPECT_FALSE(mgr.hasStoredCredentials());
}

TEST(WiFiCredentialApiTest, FactoryResetClearsCredentials) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);
    mgr.storeCredentials("net", "pw");
    EXPECT_TRUE(mgr.factoryReset());
    EXPECT_TRUE(prefs.cleared);
    EXPECT_FALSE(mgr.hasStoredCredentials());
}

// ── State machine ──────────────────────────────────────────────────────────────

TEST(WiFiStateMachineTest, ConstructionStartsDisconnected) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);
    // Before the first tick, the manager is in DISCONNECTED.
    EXPECT_EQ(mgr.getState(), WiFiState::State::DISCONNECTED);
}

TEST(WiFiStateMachineTest, InitWithNoCredentialsLandsInApMode) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;  // empty -> no creds
    WiFiManager mgr(wifi, prefs, led);
    mgr.init();  // first tick runs DISCONNECTED handler -> AP mode (no creds)
    EXPECT_EQ(mgr.getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_EQ(wifi.mode, 2);  // WIFI_AP
}

TEST(WiFiStateMachineTest, DisconnectedWithStoredCredsBeginsStaConnecting) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    prefs.ssid = "net"; prefs.pass = "pw";
    WiFiManager mgr(wifi, prefs, led);
    mgr.init();  // ticks DISCONNECTED handler
    EXPECT_EQ(wifi.mode, 1);  // WIFI_STA
    EXPECT_EQ(wifi.lastSsid, "net");
    EXPECT_EQ(wifi.lastPass, "pw");
    EXPECT_EQ(mgr.getState(), WiFiState::State::CONNECTING);
}

TEST(WiFiStateMachineTest, DisconnectedWithNoCredsGoesToApMode) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;  // empty creds
    WiFiManager mgr(wifi, prefs, led);
    mgr.init();
    EXPECT_EQ(wifi.mode, 2);  // WIFI_AP
    EXPECT_EQ(wifi.lastSsid, WiFiConfig::AP_SSID);
    EXPECT_EQ(mgr.getState(), WiFiState::State::CONNECTED_AP);
}

TEST(WiFiStateMachineTest, ConnectingTransitionsToConnectedStaOnConnectedStatus) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    prefs.ssid = "net"; prefs.pass = "pw";
    bool ntpCalled = false;
    WiFiManager mgr(wifi, prefs, led);
    mgr.setNtpInitCallback([&]() { ntpCalled = true; });
    mgr.init();  // -> CONNECTING

    wifi.statusVal = 3;  // WL_CONNECTED
    mgr.update(1);
    EXPECT_EQ(mgr.getState(), WiFiState::State::CONNECTED_STA);
    EXPECT_TRUE(ntpCalled);  // initNtp flag on CONNECTED_STA
}

TEST(WiFiStateMachineTest, OnDisconnectedAuthFailGoesToApImmediately) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);
    mgr.init();
    mgr.onDisconnected(WIFI_REASON_AUTH_FAIL);
    EXPECT_EQ(mgr.getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_FALSE(mgr.shouldRestartTcpServer());
}

TEST(WiFiStateMachineTest, OnDisconnectedFromStaGoesToReconnectingAndFlagsTcpRestart) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);
    mgr.init();
    // Force into CONNECTED_STA artificially to simulate a live connection drop.
    mgr.update(0);  // harmless tick in DISCONNECTED
    // Drive to CONNECTED_STA via CONNECTING.
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);
    ASSERT_EQ(mgr.getState(), WiFiState::State::CONNECTED_STA);

    mgr.onDisconnected(WIFI_REASON_UNSPECIFIED);
    EXPECT_EQ(mgr.getState(), WiFiState::State::RECONNECTING);
    EXPECT_TRUE(mgr.shouldRestartTcpServer());
}

TEST(WiFiStateMachineTest, ShouldRestartTcpServerFlagCanBeCleared) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    WiFiManager mgr(wifi, prefs, led);
    mgr.init();
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);
    mgr.onDisconnected(WIFI_REASON_UNSPECIFIED);
    ASSERT_TRUE(mgr.shouldRestartTcpServer());
    mgr.clearTcpServerRestartFlag();
    EXPECT_FALSE(mgr.shouldRestartTcpServer());
}

TEST(WiFiStateMachineTest, StateNameRoundTripsAllStates) {
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::DISCONNECTED), "DISCONNECTED");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::CONNECTING), "CONNECTING");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::CONNECTED_STA), "CONNECTED_STA");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::CONNECTED_AP), "CONNECTED_AP");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::RECONNECTING), "RECONNECTING");
}

TEST(WiFiStateMachineTest, TcpRestartCallbackFiresOnSetFlagTransition) {
    FakeWiFi wifi; FakePreferences prefs; FakeStatusLed led;
    bool tcpRestart = false;
    WiFiManager mgr(wifi, prefs, led);
    mgr.setTcpServerRestartCallback([&]() { tcpRestart = true; });
    mgr.init();
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);  // CONNECTED_STA sets restart flag + ntp
    EXPECT_TRUE(tcpRestart);
}

} // namespace
