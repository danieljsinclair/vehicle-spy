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
    void onEvent(std::function<void(int, WifiEventInfo*)> cb, int event) override {
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

// No-op ISerial: WiFiManager's serial-trace contract is exercised by the
// dedicated WiFiManagerSerialTraceTest suite. These state-machine / credential
// tests are agnostic to trace output, so the fake simply discards it.
class FakeSerial : public ISerial {
public:
    void println(const char*) override {}

    __attribute__((format(printf, 2, 3)))
    void printf(const char*, ...) override {}
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
    // Connected states never retry (regardless of attempts/window).
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::WIFI_CONNECTED, 10000, 0, 0));
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::WIFI_AP_MODE, 10000, 0, 0));
    // RESILIENT RECONNECT (req-1): in the aggressive first-retries window
    // (reconnectAttempts < WIFI_CONNECT_FIRST_RETRIES_COUNT) the retry interval is
    // 0, so a transient state retries IMMEDIATELY (no backoff) even at now=1ms.
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::WIFI_DISCONNECTED, 1, 0, 0));    // aggressive: immediate
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, 1000, 0, 0));   // aggressive: immediate
    // Once the aggressive window is exhausted, the normal 5000ms interval applies.
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::WIFI_DISCONNECTED, 1000, 0, WiFiConfig::WIFI_CONNECT_FIRST_RETRIES_COUNT)); // < 5000
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::WIFI_DISCONNECTED, 6000, 0, WiFiConfig::WIFI_CONNECT_FIRST_RETRIES_COUNT));   // >= 5000
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, 6000, 0, WiFiConfig::WIFI_CONNECT_FIRST_RETRIES_COUNT));
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
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);

    EXPECT_FALSE(mgr.hasStoredCredentials());
    EXPECT_TRUE(mgr.storeCredentials("MyNet", "secret"));
    EXPECT_TRUE(mgr.hasStoredCredentials());

    std::string ssid, pass;
    EXPECT_TRUE(mgr.loadCredentials(ssid, pass));
    EXPECT_EQ(ssid, "MyNet");
    EXPECT_EQ(pass, "secret");
}

TEST(WiFiCredentialApiTest, ClearCredentialsRemovesStored) {
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);

    mgr.storeCredentials("net", "pw");
    ASSERT_TRUE(mgr.hasStoredCredentials());
    EXPECT_TRUE(mgr.clearCredentials());
    EXPECT_FALSE(mgr.hasStoredCredentials());
}

TEST(WiFiCredentialApiTest, FactoryResetClearsCredentials) {
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.storeCredentials("net", "pw");
    EXPECT_TRUE(mgr.factoryReset());
    EXPECT_TRUE(prefs.cleared);
    EXPECT_FALSE(mgr.hasStoredCredentials());
}

// ── State machine ──────────────────────────────────────────────────────────────

TEST(WiFiStateMachineTest, ConstructionStartsDisconnected) {
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    // Before the first tick, the manager is in DISCONNECTED.
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_DISCONNECTED);
}

TEST(WiFiStateMachineTest, InitWithNoCredentialsLandsInApMode) {
    FakeWiFi wifi; FakePreferences prefs;  // empty -> no creds
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.init();  // first tick runs DISCONNECTED handler -> AP mode (no creds)
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_EQ(wifi.mode, 2);  // WIFI_AP
}

TEST(WiFiStateMachineTest, DisconnectedWithStoredCredsBeginsStaConnecting) {
    FakeWiFi wifi; FakePreferences prefs;
    prefs.ssid = "net"; prefs.pass = "pw";
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.init();  // ticks DISCONNECTED handler
    EXPECT_EQ(wifi.mode, 1);  // WIFI_STA
    EXPECT_EQ(wifi.lastSsid, "net");
    EXPECT_EQ(wifi.lastPass, "pw");
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTING);
}

TEST(WiFiStateMachineTest, DisconnectedWithNoCredsGoesToApMode) {
    FakeWiFi wifi; FakePreferences prefs;  // empty creds
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.init();
    EXPECT_EQ(wifi.mode, 2);  // WIFI_AP
    EXPECT_EQ(wifi.lastSsid, WiFiConfig::AP_SSID);
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_AP_MODE);
}

TEST(WiFiStateMachineTest, ConnectingTransitionsToConnectedStaOnConnectedStatus) {
    FakeWiFi wifi; FakePreferences prefs;
    prefs.ssid = "net"; prefs.pass = "pw";
    bool ntpCalled = false;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.setNtpInitCallback([&]() { ntpCalled = true; });
    mgr.init();  // -> CONNECTING

    wifi.statusVal = 3;  // WL_CONNECTED
    mgr.update(1);
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_TRUE(ntpCalled);  // initNtp flag on CONNECTED_STA
}

TEST(WiFiStateMachineTest, OnDisconnectedAuthFailGoesToApImmediately) {
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.init();
    mgr.onDisconnected(WIFI_REASON_AUTH_FAIL);
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_FALSE(mgr.shouldRestartTcpServer());
}

TEST(WiFiStateMachineTest, OnDisconnectedFromStaGoesToReconnectingAndFlagsTcpRestart) {
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.init();
    // Force into CONNECTED_STA artificially to simulate a live connection drop.
    mgr.update(0);  // harmless tick in DISCONNECTED
    // Drive to CONNECTED_STA via CONNECTING.
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);
    ASSERT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTED);

    mgr.onDisconnected(WIFI_REASON_UNSPECIFIED);
    EXPECT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(mgr.shouldRestartTcpServer());
}

TEST(WiFiStateMachineTest, ShouldRestartTcpServerFlagCanBeCleared) {
    FakeWiFi wifi; FakePreferences prefs;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
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
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_DISCONNECTED), "WIFI_DISCONNECTED");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_CONNECTING), "WIFI_CONNECTING");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_CONNECTED), "WIFI_CONNECTED");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_AP_MODE), "WIFI_AP_MODE");
    EXPECT_STREQ(WiFiManager::stateName(WiFiState::State::WIFI_CONNECTING), "WIFI_CONNECTING");
}

TEST(WiFiStateMachineTest, TcpRestartCallbackFiresOnSetFlagTransition) {
    FakeWiFi wifi; FakePreferences prefs;
    bool tcpRestart = false;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.setTcpServerRestartCallback([&]() { tcpRestart = true; });
    mgr.init();
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);  // CONNECTED_STA sets restart flag + ntp
    EXPECT_TRUE(tcpRestart);
}

// RESILIENT RECONNECT (req-2): on a (re)connect the WiFiManager must invoke the
// discovery-reset callback so the app is re-broadcast at the SHORT/FAST cadence
// (reset any backoff tier) and re-finds the possibly-new IP quickly.
TEST(WiFiStateMachineTest, DiscoveryResetFiresOnFirstConnect) {
    FakeWiFi wifi; FakePreferences prefs;
    int discoveryResets = 0;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.setDiscoveryResetCallback([&]() { ++discoveryResets; });
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();  // -> CONNECTING
    EXPECT_EQ(discoveryResets, 0);  // not yet connected
    wifi.statusVal = 3;  // WL_CONNECTED
    mgr.update(1);  // -> CONNECTED_STA
    EXPECT_EQ(discoveryResets, 1);  // discovery backoff reset on connect
}

TEST(WiFiStateMachineTest, DiscoveryResetFiresOnReconnectAfterDrop) {
    FakeWiFi wifi; FakePreferences prefs;
    int discoveryResets = 0;
    FakeSerial serial;
    WiFiManager mgr(wifi, prefs, serial);
    mgr.setDiscoveryResetCallback([&]() { ++discoveryResets; });
    prefs.ssid = "net"; prefs.pass = "pw";
    mgr.init();
    wifi.statusVal = 3;
    mgr.update(1);  // -> CONNECTED_STA (1 reset)
    ASSERT_EQ(discoveryResets, 1);

    // Drop to RECONNECTING, then re-associate.
    mgr.onDisconnected(WIFI_REASON_UNSPECIFIED);
    ASSERT_EQ(mgr.getState(), WiFiState::State::WIFI_CONNECTING);
    wifi.statusVal = 3;  // re-associated (possibly new IP)
    mgr.update(2);  // -> CONNECTED_STA again
    EXPECT_EQ(discoveryResets, 2);  // discovery backoff reset AGAIN on reconnect
}

} // namespace
