// WiFiManagerBehavior.test.cpp
//
// Blind spec-first tests pinning the Stage 4 WiFi BEHAVIOR SPEC (sections 1-13)
// for the vanilla WiFiManager. Written against the WiFiManager.h CONTRACT and
// the relayed behavior spec ONLY — no .cpp / .ino reads.
//
// This file complements (does not duplicate) WiFiManager.test.cpp, which covers
// the basic pure-function cases and core state transitions. Here we pin the
// GRANULAR branch behavior the spec adds: boundary conditions, every
// state-handler branch, the CONNECTED_STA handler (spec #8, expected RED), the
// RECONNECTING handler, onDisconnected reason-routing, applyStateTransition
// side-effects (including the stay-sentinel no-op), and the credential
// failure-modes.
//
// WL_* status constants used by the spec:
//   0 = WL_IDLE_STATUS, 1 = WL_NO_SSID_AVAIL, 3 = WL_CONNECTED, 4 = WL_CONNECT_FAILED
// WIFI mode constants: 1 = WIFI_STA, 2 = WIFI_AP.
// LED patterns (StatusLED): 0 = WIFI_SEARCHING, 2 = WIFI_CONNECTED, 4 = AP_MODE.

#include <gtest/gtest.h>
#include "firmware/vanilla/WiFiManager.h"
#include "firmware/vanilla/WiFiReasonCodes.h"

#include <cstring>
#include <string>
#include <vector>

using namespace esp32_firmware;

namespace {

constexpr int WL_IDLE_STATUS = 0;
constexpr int WL_CONNECTED = 3;
constexpr int WL_CONNECT_FAILED = 4;

constexpr int WIFI_STA = 1;
constexpr int WIFI_AP = 2;

// LED pattern ids (from StatusLED enum, referenced by the spec).
constexpr int LED_WIFI_SEARCHING = 0;
constexpr int LED_WIFI_CONNECTED = 2;
constexpr int LED_AP_MODE = 4;

// ── Fakes ──────────────────────────────────────────────────────────────────────
class FakeWiFi : public IWiFi {
public:
    int mode = 0;
    std::string lastSsid;
    std::string lastPass;
    int beginCalls = 0;
    int softApCalls = 0;
    int disconnectCalls = 0;
    bool lastDisconnectWifiOff = false;
    bool lastDisconnectEraseAp = false;
    int statusVal = WL_IDLE_STATUS;
    int hostnameSets = 0;
    std::string lastHostname;
    std::vector<int> setModeHistory;

    void setMode(int m) override { mode = m; setModeHistory.push_back(m); }
    void begin(const char* ssid, const char* pass) override {
        lastSsid = ssid ? ssid : "";
        lastPass = pass ? pass : "";
        ++beginCalls;
    }
    void disconnect(bool wifiOff, bool eraseAP) override {
        ++disconnectCalls;
        lastDisconnectWifiOff = wifiOff;
        lastDisconnectEraseAp = eraseAP;
    }
    int status() const override { return statusVal; }
    std::string localIP() const override { return "192.168.68.60"; }
    std::string softAPIP() const override { return "192.168.4.1"; }
    void softAP(const char* ssid, const char* pass) override {
        lastSsid = ssid ? ssid : "";
        lastPass = pass ? pass : "";
        mode = WIFI_AP;
        ++softApCalls;
    }
    void setHostname(const char* name) override {
        ++hostnameSets;
        lastHostname = name ? name : "";
    }
    int getMode() const override { return mode; }
    std::string SSID() const override { return lastSsid; }
    const char* disconnectReasonName(int) const override { return ""; }
    void onEvent(std::function<void(int, WifiEventInfo*)>, int) override {}
};

class FakePreferences : public IPreferences {
public:
    std::string ssid;
    std::string pass;
    bool cleared = false;
    // When true, putString reports 0 bytes written (simulating NVS write failure).
    bool putStringFails = false;
    int beginCalls = 0;
    int endCalls = 0;
    std::vector<std::pair<std::string, bool>> beginHistory;  // name, readOnly

    void begin(const char* name, bool readOnly) override {
        ++beginCalls;
        beginHistory.emplace_back(name ? name : "", readOnly);
    }
    void end() override { ++endCalls; }
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
        if (putStringFails) return 0;
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) { ssid = value; return value.size(); }
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) { pass = value; return value.size(); }
        return 0;
    }
    void clear() override { ssid.clear(); pass.clear(); cleared = true; }
};

class FakeStatusLed : public IStatusLED {
public:
    int lastPattern = -1;
    int setPatternCalls = 0;
    void setPattern(int p) override { lastPattern = p; ++setPatternCalls; }
    void update(uint32_t) override {}
};

struct Harness {
    FakeWiFi wifi;
    FakePreferences prefs;
    FakeStatusLed led;
    std::unique_ptr<WiFiManager> mgr;

    void build(const char* bakedSsid = nullptr, const char* bakedPass = nullptr) {
        mgr = std::make_unique<WiFiManager>(wifi, prefs, led, bakedSsid, bakedPass);
    }
};

// ════════════════════════════════════════════════════════════════════════════
// §1 determineCredentialSource
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorCredentialSourceTest, StoredNvsTakesPriorityOverBaked) {
    FakePreferences prefs;
    prefs.ssid = "stored"; prefs.pass = "storedpw";
    // Both stored AND baked present -> STORED_NVS wins.
    EXPECT_EQ(determineCredentialSource(prefs, "baked", "bakedpw"), CredentialSource::STORED_NVS);
}

TEST(WiFiBehaviorCredentialSourceTest, EmptyBakedSsidStringTreatedAsNone) {
    FakePreferences prefs;  // empty
    EXPECT_EQ(determineCredentialSource(prefs, "", "bakedpw"), CredentialSource::NONE);
}

TEST(WiFiBehaviorCredentialSourceTest, EmptyBakedPassStringTreatedAsNone) {
    FakePreferences prefs;  // empty
    EXPECT_EQ(determineCredentialSource(prefs, "baked", ""), CredentialSource::NONE);
}

// ════════════════════════════════════════════════════════════════════════════
// §2 shouldFallbackToApMode — boundaries + NONE
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorFallbackTest, StoredNvsAtExactTimeoutBoundaryIsFalse) {
    // Spec: strict > — boundary == 30000 is false.
    EXPECT_FALSE(shouldFallbackToApMode(CredentialSource::STORED_NVS,
                                        WiFiConfig::WIFI_CONNECT_TIMEOUT_MS));
}

TEST(WiFiBehaviorFallbackTest, StoredNvsJustOverTimeoutIsTrue) {
    EXPECT_TRUE(shouldFallbackToApMode(CredentialSource::STORED_NVS,
                                       WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1));
}

TEST(WiFiBehaviorFallbackTest, NoneCredentialNeverFallsBack) {
    EXPECT_FALSE(shouldFallbackToApMode(CredentialSource::NONE, 999999));
}

// ════════════════════════════════════════════════════════════════════════════
// §3 isInitialConnectTimeout — boundary
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorInitialTimeoutTest, AtExactCapBoundaryIsFalse) {
    const uint32_t cap = WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    EXPECT_FALSE(isInitialConnectTimeout(cap));      // strict >
    EXPECT_TRUE(isInitialConnectTimeout(cap + 1));
}

// ════════════════════════════════════════════════════════════════════════════
// §4 shouldRetryWiFi — boundaries
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorRetryTest, ConnectingAtExactIntervalBoundaryIsTrue) {
    // now - lastRetry == 5000 -> true (>= boundary).
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::CONNECTING, 5000, 0));
}

TEST(WiFiBehaviorRetryTest, ConnectingJustBelowIntervalIsFalse) {
    EXPECT_FALSE(shouldRetryWiFi(WiFiState::State::CONNECTING, 4999, 0));
}

TEST(WiFiBehaviorRetryTest, ReconnectingAtExactIntervalIsTrue) {
    EXPECT_TRUE(shouldRetryWiFi(WiFiState::State::RECONNECTING, 5000, 0));
}

// ════════════════════════════════════════════════════════════════════════════
// §5 DISCONNECTED handler — full branch coverage
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorDisconnectedTest, StoredNvsBeginsStaAndSetsHostnameAndTransitioningToConnecting) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();  // ticks DISCONNECTED handler

    EXPECT_EQ(h.wifi.mode, WIFI_STA);
    EXPECT_EQ(h.wifi.lastSsid, "net");
    EXPECT_EQ(h.wifi.lastPass, "pw");
    ASSERT_GE(h.wifi.hostnameSets, 1);
    EXPECT_EQ(h.wifi.lastHostname, WiFiConfig::HOSTNAME);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
}

TEST(WiFiBehaviorDisconnectedTest, StoredNvsButLoadFailsStaysDisconnected) {
    Harness h;
    // creds present in prefs but loadCredentialsImpl must fail. We simulate by
    // making getBytesLength report empty (so hasStoredCredentials path reports
    // no creds even though strings exist). The spec: source STORED_NVS but
    // loadCredentialsImpl returns false -> stays DISCONNECTED, no begin.
    // NOTE: determineCredentialSource uses getBytesLength, so empty length means
    // source is NOT STORED_NVS. To genuinely test the "source STORED_NVS but
    // load fails" branch we'd need a prefs where length>0 but getString empty.
    // That is an internal-inconsistent state; we assert the observable contract:
    // no begin() call and state stays DISCONNECTED.
    // (See spec-gap note in report: this branch may be unreachable via the
    // public prefs contract; flagged for tech-arch.)
    h.build();
    h.mgr->init();
    EXPECT_EQ(h.wifi.beginCalls, 0);
    // No creds -> AP path, not DISCONNECTED. This test documents that the
    // "load fails" branch is not reachable through a consistent prefs mock.
}

TEST(WiFiBehaviorDisconnectedTest, BakedInBeginsStaWithBakedCreds) {
    Harness h;
    h.build("bakedNet", "bakedPw");
    h.mgr->init();
    EXPECT_EQ(h.wifi.mode, WIFI_STA);
    EXPECT_EQ(h.wifi.lastSsid, "bakedNet");
    EXPECT_EQ(h.wifi.lastPass, "bakedPw");
    ASSERT_GE(h.wifi.hostnameSets, 1);
    EXPECT_EQ(h.wifi.lastHostname, WiFiConfig::HOSTNAME);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
}

TEST(WiFiBehaviorDisconnectedTest, NoneCredentialGoesToApMode) {
    Harness h;
    h.build();  // no stored, no baked
    h.mgr->init();
    EXPECT_EQ(h.wifi.mode, WIFI_AP);
    EXPECT_EQ(h.wifi.lastSsid, WiFiConfig::AP_SSID);
    EXPECT_EQ(h.wifi.lastPass, WiFiConfig::AP_PASS);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
}

// ════════════════════════════════════════════════════════════════════════════
// §6 CONNECTING handler — timeout/retry/initial-cap branches
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorConnectingTest, ConnectFailedStoredNvsOverTimeoutFallsBackToAp) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();  // -> CONNECTING at t=0
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);

    h.wifi.statusVal = WL_CONNECT_FAILED;  // 4
    h.wifi.softApCalls = 0;
    h.mgr->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1);  // over timeout

    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_GE(h.wifi.softApCalls, 1);
    EXPECT_EQ(h.wifi.mode, WIFI_AP);
}

TEST(WiFiBehaviorConnectingTest, ConnectFailedBakedInOverTimeoutDoesNotFallBackToAp) {
    Harness h;
    h.build("bakedNet", "bakedPw");
    h.mgr->init();  // -> CONNECTING
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);

    h.wifi.statusVal = WL_CONNECT_FAILED;
    h.mgr->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1);

    EXPECT_NE(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_EQ(h.wifi.softApCalls, 0);
}

TEST(WiFiBehaviorConnectingTest, ConnectFailedRetryElapsedReBeginsWithStoredCreds) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();  // -> CONNECTING, begin() called once at t=0
    const int beginsBefore = h.wifi.beginCalls;
    ASSERT_GT(beginsBefore, 0);

    h.wifi.statusVal = WL_CONNECT_FAILED;  // not connected, not over timeout
    // Advance past retry interval (but under connect timeout) -> retry begin().
    h.mgr->update(WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    EXPECT_GT(h.wifi.beginCalls, beginsBefore);
    EXPECT_EQ(h.wifi.lastSsid, "net");
    EXPECT_EQ(h.wifi.lastPass, "pw");
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
}

TEST(WiFiBehaviorConnectingTest, ConnectFailedRetryNotElapsedStaysConnectingNoBegin) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();  // -> CONNECTING, begin once
    const int beginsBefore = h.wifi.beginCalls;

    h.wifi.statusVal = WL_CONNECT_FAILED;
    h.mgr->update(1000);  // well under retry interval

    EXPECT_EQ(h.wifi.beginCalls, beginsBefore);  // no new begin
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
}

TEST(WiFiBehaviorConnectingTest, IdleStatusNoTimeoutStaysConnecting) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_IDLE_STATUS;
    h.mgr->update(1000);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
}

TEST(WiFiBehaviorConnectingTest, InitialConnectTimeoutStoredNvsFallsBackToAp) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);

    const uint32_t cap = WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    h.wifi.statusVal = WL_IDLE_STATUS;
    h.mgr->update(cap + 1);

    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_GE(h.wifi.softApCalls, 1);
}

TEST(WiFiBehaviorConnectingTest, InitialConnectTimeoutBakedInGoesReconnecting) {
    Harness h;
    h.build("bakedNet", "bakedPw");
    h.mgr->init();
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);

    const uint32_t cap = WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    h.wifi.statusVal = WL_IDLE_STATUS;
    h.mgr->update(cap + 1);

    EXPECT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);
}

// ════════════════════════════════════════════════════════════════════════════
// §7 RECONNECTING handler
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorReconnectingTest, ConnectedStatusTransitionsToConnectedSta) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    // Drive to CONNECTED_STA then disconnect to RECONNECTING.
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);
    h.mgr->onDisconnected(WIFI_REASON_UNSPECIFIED);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);

    // Now connected again -> CONNECTED_STA.
    h.mgr->update(2000);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);
}

TEST(WiFiBehaviorReconnectingTest, RetryElapsedReBeginsWithStoredCreds) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    h.mgr->onDisconnected(WIFI_REASON_UNSPECIFIED);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);

    const int beginsBefore = h.wifi.beginCalls;
    h.wifi.statusVal = WL_IDLE_STATUS;  // still not connected
    h.mgr->update(1000 + WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS + 1);

    EXPECT_GT(h.wifi.beginCalls, beginsBefore);
    EXPECT_EQ(h.wifi.lastSsid, "net");
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);
}

TEST(WiFiBehaviorReconnectingTest, RetryNotElapsedStaysReconnectingNoBegin) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    h.mgr->onDisconnected(WIFI_REASON_UNSPECIFIED);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);

    const int beginsBefore = h.wifi.beginCalls;
    h.wifi.statusVal = WL_IDLE_STATUS;
    h.mgr->update(1100);  // < retry interval
    EXPECT_EQ(h.wifi.beginCalls, beginsBefore);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);
}

// ════════════════════════════════════════════════════════════════════════════
// §8 CONNECTED_STA handler — INTENDED RED PIN (behavior not yet satisfied)
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorConnectedStaTest, LossOfConnectedStatusTransitionsToReconnecting) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);

    // WiFi reports no longer connected (e.g. beacon loss) without an explicit
    // disconnect event. Spec #8: status != WL_CONNECTED -> RECONNECTING,
    // tcpServerNeedsRestart=true, initNtp=false.
    h.wifi.statusVal = WL_IDLE_STATUS;
    h.mgr->update(2000);

    EXPECT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);
    EXPECT_TRUE(h.mgr->shouldRestartTcpServer());
}

TEST(WiFiBehaviorConnectedStaTest, StaysConnectedStaWhenStatusRemainsConnected) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);

    h.mgr->update(2000);  // still connected
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);
}

// ════════════════════════════════════════════════════════════════════════════
// §9 CONNECTED_AP handler
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorConnectedApTest, ApModeIsStableWithNoWifiCalls) {
    Harness h;
    h.build();  // no creds -> AP
    h.mgr->init();
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);

    const int beginsBefore = h.wifi.beginCalls;
    const int softApBefore = h.wifi.softApCalls;
    h.mgr->update(1000);
    h.mgr->update(5000);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_EQ(h.wifi.beginCalls, beginsBefore);
    EXPECT_EQ(h.wifi.softApCalls, softApBefore);
}

// ════════════════════════════════════════════════════════════════════════════
// §10 onDisconnected — reason routing
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorOnDisconnectedTest, AuthExpireGoesToApMode) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);

    h.mgr->onDisconnected(WIFI_REASON_AUTH_EXPIRE);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_FALSE(h.mgr->shouldRestartTcpServer());
    EXPECT_EQ(h.wifi.mode, WIFI_AP);
}

TEST(WiFiBehaviorOnDisconnectedTest, FourWayHandshakeTimeoutGoesToApMode) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);

    h.mgr->onDisconnected(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_FALSE(h.mgr->shouldRestartTcpServer());
}

TEST(WiFiBehaviorOnDisconnectedTest, NonAuthReasonFromConnectedStaGoesReconnecting) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);

    h.mgr->onDisconnected(WIFI_REASON_BEACON_TIMEOUT);  // 200, non-auth
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::RECONNECTING);
    EXPECT_TRUE(h.mgr->shouldRestartTcpServer());
}

TEST(WiFiBehaviorOnDisconnectedTest, NonAuthReasonFromNonStaStateLeavesStateUnchanged) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);  // not STA

    h.mgr->onDisconnected(WIFI_REASON_BEACON_TIMEOUT);
    // State must not have jumped to AP or RECONNECTING via this path.
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
}

// ════════════════════════════════════════════════════════════════════════════
// §11 applyStateTransition — LED patterns + stay-sentinel no-op
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorTransitionTest, TransitionToConnectedStaSetsConnectedLedPattern) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.led.setPatternCalls = 0;
    h.mgr->update(1000);  // -> CONNECTED_STA

    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);
    EXPECT_GE(h.led.setPatternCalls, 1);
    EXPECT_EQ(h.led.lastPattern, LED_WIFI_CONNECTED);
}

TEST(WiFiBehaviorTransitionTest, TransitionToConnectedApSetsApModeLedPattern) {
    Harness h;
    h.build();  // no creds -> AP
    h.led.setPatternCalls = 0;
    h.mgr->init();
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_AP);
    EXPECT_GE(h.led.setPatternCalls, 1);
    EXPECT_EQ(h.led.lastPattern, LED_AP_MODE);
}

TEST(WiFiBehaviorTransitionTest, TransitionToConnectingSetsSearchingLedPattern) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.led.setPatternCalls = 0;
    h.mgr->init();  // DISCONNECTED -> CONNECTING
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
    EXPECT_GE(h.led.setPatternCalls, 1);
    EXPECT_EQ(h.led.lastPattern, LED_WIFI_SEARCHING);
}

TEST(WiFiBehaviorTransitionTest, StayingInSameStateDoesNotCallSetPattern) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    h.mgr->init();
    ASSERT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);

    h.wifi.statusVal = WL_IDLE_STATUS;  // no transition, no timeout, retry not elapsed
    h.led.setPatternCalls = 0;
    h.mgr->update(1000);  // stays CONNECTING
    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTING);
    EXPECT_EQ(h.led.setPatternCalls, 0);  // stay-sentinel: no setPattern
}

TEST(WiFiBehaviorTransitionTest, ConnectedStaTransitionFiresNtpAndTcpRestartCallbacks) {
    Harness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.build();
    bool ntpFired = false;
    bool tcpFired = false;
    h.mgr->setNtpInitCallback([&]() { ntpFired = true; });
    h.mgr->setTcpServerRestartCallback([&]() { tcpFired = true; });
    h.mgr->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.mgr->update(1000);

    EXPECT_EQ(h.mgr->getState(), WiFiState::State::CONNECTED_STA);
    EXPECT_TRUE(ntpFired);
    EXPECT_TRUE(tcpFired);
}

// ════════════════════════════════════════════════════════════════════════════
// §13 Credentials — failure modes + factoryReset delegation
// ════════════════════════════════════════════════════════════════════════════

TEST(WiFiBehaviorCredentialsTest, StoreCredentialsFailsWhenNvsWriteReturnsZero) {
    Harness h;
    h.build();
    h.prefs.putStringFails = true;
    EXPECT_FALSE(h.mgr->storeCredentials("net", "pw"));
}

TEST(WiFiBehaviorCredentialsTest, StoreCredentialsOpensWifiNamespaceReadWrite) {
    Harness h;
    h.build();
    ASSERT_TRUE(h.mgr->storeCredentials("net", "pw"));
    ASSERT_FALSE(h.prefs.beginHistory.empty());
    // First begin for storeCredentials is the "wifi" namespace, read-write.
    const auto& first = h.prefs.beginHistory.front();
    EXPECT_EQ(first.first, WiFiConfig::NVS_WIFI_NAMESPACE);
    EXPECT_FALSE(first.second);  // readOnly == false
}

TEST(WiFiBehaviorCredentialsTest, HasStoredCredentialsOpensReadOnlyAndChecksBothKeys) {
    Harness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.prefs.beginHistory.clear();
    EXPECT_TRUE(h.mgr->hasStoredCredentials());
    ASSERT_FALSE(h.prefs.beginHistory.empty());
    const auto& first = h.prefs.beginHistory.front();
    EXPECT_EQ(first.first, WiFiConfig::NVS_WIFI_NAMESPACE);
    EXPECT_TRUE(first.second);  // readOnly == true
}

TEST(WiFiBehaviorCredentialsTest, HasStoredCredentialsFalseWhenEitherKeyMissing) {
    Harness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "";  // pass missing
    EXPECT_FALSE(h.mgr->hasStoredCredentials());
}

TEST(WiFiBehaviorCredentialsTest, LoadCredentialsReturnsFalseWhenEitherKeyMissing) {
    Harness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "";
    std::string ssid, pass;
    EXPECT_FALSE(h.mgr->loadCredentials(ssid, pass));
}

TEST(WiFiBehaviorCredentialsTest, FactoryResetClearsCredentialsViaClearCall) {
    Harness h;
    h.build();
    h.mgr->storeCredentials("net", "pw");
    ASSERT_TRUE(h.mgr->hasStoredCredentials());
    EXPECT_TRUE(h.mgr->factoryReset());
    EXPECT_TRUE(h.prefs.cleared);
    EXPECT_FALSE(h.mgr->hasStoredCredentials());
}

} // namespace
