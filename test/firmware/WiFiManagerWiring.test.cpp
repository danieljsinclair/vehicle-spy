// WiFiManagerWiring.test.cpp
//
// Blind spec-first tests for Stage 4 of the .ino -> vanilla extraction:
// wiring WiFiManager into FirmwareApp (the orchestrator the .ino delegates to).
//
// These tests pin the INTEGRATION behaviors the inline .ino WiFi state machine
// used to own and that Stage 4 must preserve when delegation moves into
// FirmwareApp + WiFiManager. They are written against the vanilla HEADERS only
// (the contracts in firmware/vanilla/*.h and the WiFiConfig / NtpConfig /
// DiscoveryConfig constants) — NOT against any .cpp implementation. They assert
// specified behavior, not implementation detail.
//
// Spec sources (headers only):
//   - WiFiManager.h: WiFiState, StateTransition, IWiFi, IPreferences, IStatusLED,
//     WiFiConfig (retry/timeout/AP creds/hostname), CredentialSource, public API.
//   - FirmwareApp.h: orchestrator contract, deferred NTP/discovery start,
//     onWiFiDisconnected, shouldRestartTcpServer, credential pass-through.
//   - NtpTimeSync.h: NtpConfig, ISntp, ITimeNtp, startIfWiFiConnected.
//   - WiFiReasonCodes.h: WIFI_REASON_* constants.
//
// What is already covered elsewhere and intentionally NOT duplicated here:
//   - WiFiManager unit-level state transitions (test/firmware/WiFiManager.test.cpp)
//   - FirmwareApp init lifecycle / basic callback bridging
//     (test/firmware/FirmwareApp.test.cpp)
//
// These tests fill the Stage-4-specific gaps: connect-with-stored-creds, NTP
// trigger on connect, AP fallback after connect timeout, retry-on-disconnect,
// initial-connect timeout cap, baked-in credential usage.

#include <gtest/gtest.h>
#include "firmware/vanilla/FirmwareApp.h"
#include "firmware/vanilla/WiFiReasonCodes.h"

#include <array>
#include <string>

using namespace esp32_firmware;

namespace {

// Arduino WL_* status values used by the spec (IWiFi::status()).
constexpr int WL_IDLE_STATUS = 0;
constexpr int WL_CONNECTED = 3;  // matches existing test suite convention
constexpr int WL_CONNECT_FAILED = 4;

// WIFI_AP_STA mode constant used by the spec (IWiFi::setMode / getMode).
constexpr int WIFI_AP = 2;
constexpr int WIFI_STA = 1;

// ── Fakes (owned here so the Stage-4 assertions can inspect begin() args,
//    sntp.init() invocation, and tick-by-tick state without disturbing the
//    shared FirmwareApp.test.cpp harness). ────────────────────────────────
class FakeWiFi : public IWiFi {
public:
    int mode = 0;
    std::string lastSsid;
    std::string lastPass;
    int beginCalls = 0;
    int softApCalls = 0;
    int statusVal = WL_IDLE_STATUS;
    std::string localIpVal = "192.168.68.60";
    int hostnameSets = 0;

    void setMode(int m) override { mode = m; }
    void begin(const char* ssid, const char* pass) override {
        lastSsid = ssid ? ssid : "";
        lastPass = pass ? pass : "";
        ++beginCalls;
    }
    void disconnect(bool, bool) override {}
    int status() const override { return statusVal; }
    std::string localIP() const override { return localIpVal; }
    std::string softAPIP() const override { return "192.168.4.1"; }
    void softAP(const char* ssid, const char* pass) override {
        lastSsid = ssid ? ssid : "";
        lastPass = pass ? pass : "";
        mode = WIFI_AP;
        ++softApCalls;
    }
    void setHostname(const char*) override { ++hostnameSets; }
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
    void setPattern(int p) override { lastPattern = p; }
    void update(uint32_t) override {}
};

class FakeWiFiDiscovery : public IWiFiDiscovery {
public:
    int mode = WIFI_STA;
    int getMode() const override { return mode; }
    int status() const override { return WL_IDLE_STATUS; }
    std::string broadcastIP() const override { return "192.168.1.255"; }
};

class FakeUdp : public IUdp {
public:
    void begin(uint16_t) override {}
    int beginPacket(const std::string&, uint16_t) override { return 1; }
    size_t write(const uint8_t*, size_t len) override { return len; }
    int endPacket() override { return 1; }
};

class FakeTime : public ITime {
public:
    uint64_t ts = 1700000000;
    uint32_t m = 0;
    uint64_t getCurrentTimestamp() const override { return ts; }
    uint32_t millis() const override { return m; }
};

class FakeSntp : public ISntp {
public:
    int initCalls = 0;
    // enabled()==false mirrors real ArduinoSntp on fresh boot (SNTP not yet
    // running). NtpTimeSync::init() guards `if (sntp_.enabled()) return;`, so a
    // true default would make init() early-return and initCalls would stay 0
    // regardless of whether FirmwareApp bridges the NTP-init callback — a
    // false-red. false lets init() proceed so initCalls reflects the bridge.
    bool enabledVal = false;
    bool enabled() const override { return enabledVal; }
    void setOperatingMode(int) override {}
    void setServerName(int, const char*) override {}
    void setSyncMode(int) override {}
    void setSyncInterval(int32_t) override {}
    void setTimeSyncNotificationCallback(std::function<void(struct timeval*)>) override {}
    void init() override { ++initCalls; }
};

class FakeTimeNtp : public ITimeNtp {
public:
    time_t time(time_t* t) override { if (t) *t = 0; return 0; }
    void setenv(const char*, const char*, int) override {}
    void tzset() override {}
    void gmtime_r(const time_t*, struct tm*) override {}
    size_t strftime(char*, size_t, const char*, const struct tm*) override { return 0; }
};

constexpr std::array<uint8_t, 16> kDeviceId = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

class FakeCanDriver : public ICanDriver {
public:
    int driverInstall(CanGeneralConfig*, CanTimingConfig*, CanFilterConfig*) override { return 0; }
    int start() override { return 0; }
    int receive(CanFrame*, uint32_t) override { return -1; }
};

class FakeTcpClient : public ITcpClient {
public:
    bool connected() const override { return false; }
    size_t print(const char*) override { return 0; }
    void flush() override {}
};

class FakeSerialCan : public ISerialCan {
public:
    size_t print(const char*) override { return 0; }
    void flush() override {}
};

// Harness with optional baked-in credentials (Stage 4 exercises both the
// stored-NVS path and the baked-in fallback).
struct WiringHarness {
    FakeWiFi wifi;
    FakePreferences prefs;
    FakeStatusLed led;
    FakeWiFiDiscovery wifiDisc;
    FakeUdp udp;
    FakeTime time;
    FakeSntp sntp;
    FakeTimeNtp timeNtp;
    FakeCanDriver canDriver;
    FakeTcpClient tcpClient;
    FakeSerialCan serialCan;
    std::unique_ptr<FirmwareApp> app;

    void build(const char* bakedSsid = nullptr, const char* bakedPass = nullptr) {
        app = std::make_unique<FirmwareApp>(
            wifi, prefs, led, wifiDisc, udp, time, sntp, timeNtp,
            kDeviceId, CanBridgeDeps{canDriver, tcpClient, serialCan},
            bakedSsid, bakedPass);
    }
};

// ── Stage 4: connect with stored credentials ────────────────────────────────

// Spec: when stored NVS credentials exist, FirmwareApp drives the WiFiManager
// to bring WiFi up in STA mode using exactly those stored credentials.
TEST(WiFiManagerWiringTest, ConnectsWithStoredCredentialsInStaMode) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "manht2";
    h.prefs.pass = "luckyshoe478";
    h.app->init();
    h.app->update(0);  // first tick: DISCONNECTED handler reads creds -> STA begin

    EXPECT_EQ(h.wifi.mode, WIFI_STA);
    ASSERT_GE(h.wifi.beginCalls, 1);
    EXPECT_EQ(h.wifi.lastSsid, "manht2");
    EXPECT_EQ(h.wifi.lastPass, "luckyshoe478");
    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTING));
}

// ── Stage 4: NTP sync trigger on connect ─────────────────────────────────────

// Spec (FirmwareApp.h:196-198): NTP sync is deferred until WiFi is connected.
// The WiFiManager NTP-init callback is the trigger. So reaching CONNECTED_STA
// must cause sntp.init() to be invoked (NtpTimeSync started).
TEST(WiFiManagerWiringTest, NtpSyncStartsWhenWifiReachesConnectedSta) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    // FakeSntp.enabled()==false mirrors real ArduinoSntp on fresh boot (SNTP not
    // yet running). NtpTimeSync::init() guards `if (sntp_.enabled()) return;`, so
    // only with enabled()==false does init() proceed to call sntp_.init(). This
    // makes initCalls a faithful signal of whether FirmwareApp bridged WiFiManager's
    // NTP-init callback through to NtpTimeSync when WiFi reached CONNECTED_STA.
    ASSERT_FALSE(h.sntp.enabledVal);
    h.app->init();

    EXPECT_EQ(h.sntp.initCalls, 0);  // not started at boot / while connecting
    h.app->update(0);  // -> CONNECTING
    EXPECT_EQ(h.sntp.initCalls, 0);  // still not connected

    h.wifi.statusVal = WL_CONNECTED;
    h.app->update(1000);  // -> CONNECTED_STA, NTP-init callback fires
    ASSERT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_STA));
    EXPECT_GE(h.sntp.initCalls, 1);
}

// Spec: NTP must NOT start while in AP mode (no STA connection, no internet).
TEST(WiFiManagerWiringTest, NtpSyncDoesNotStartInApMode) {
    WiringHarness h;
    h.build();
    // no stored creds, no baked creds -> AP fallback
    h.app->init();
    h.app->update(0);
    ASSERT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_AP));
    EXPECT_EQ(h.sntp.initCalls, 0);
}

// ── Stage 4: fallback to AP mode after connect timeout ──────────────────────

// Spec §6: STORED_NVS + status==WL_CONNECT_FAILED(4) [or WL_NO_SSID_AVAIL(1)]
// + duration > WIFI_CONNECT_TIMEOUT_MS (30000) -> AP fallback. This is the
// connect-timeout fallback (distinct from the 5-min initial-connect cap below,
// which fires on isInitialConnectTimeout regardless of status).
TEST(WiFiManagerWiringTest, FallsBackToApModeAfterConnectTimeoutWithStoredCreds) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.app->init();
    h.app->update(0);  // -> CONNECTING

    // Explicit connect failure (status 4) past the 30s connect timeout -> AP.
    h.wifi.statusVal = WL_CONNECT_FAILED;
    h.app->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1);

    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_AP));
    EXPECT_EQ(h.wifi.mode, WIFI_AP);
    EXPECT_GE(h.wifi.softApCalls, 1);
    EXPECT_EQ(h.wifi.lastSsid, WiFiConfig::AP_SSID);
}

// Spec §6 (corrected): the 30s connect-timeout AP fallback fires ONLY on
// status==WL_CONNECT_FAILED||WL_NO_SSID_AVAIL. On idle status (WL_IDLE_STATUS)
// past 30000ms, the manager stays CONNECTING — the 5-min isInitialConnectTimeout
// cap applies later, not the 30s fallback. This pins the corrected spec (and the
// divergence from the 30s-failed-status path above).
TEST(WiFiManagerWiringTest, IdleStatusPastConnectTimeoutStaysConnectingNotAp) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.app->init();
    h.app->update(0);  // -> CONNECTING

    // Idle status just past the 30s connect timeout — NOT a failure status, so
    // the 30s AP fallback must not fire.
    h.wifi.statusVal = WL_IDLE_STATUS;
    h.app->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1);

    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTING));
    EXPECT_EQ(h.wifi.softApCalls, 0);
}

// Spec: baked-in credentials do NOT fall back to AP on timeout — they keep
// retrying (shouldFallbackToApMode returns false for BAKED_IN). This pins the
// divergence between stored-NVS and baked-in behavior at the wiring level.
TEST(WiFiManagerWiringTest, BakedInCredsDoNotFallBackToApOnTimeout) {
    WiringHarness h;
    h.build("bakedNet", "bakedPw");
    h.app->init();
    h.app->update(0);  // -> CONNECTING with baked creds
    ASSERT_EQ(h.wifi.lastSsid, "bakedNet");

    h.wifi.statusVal = WL_IDLE_STATUS;
    h.app->update(WiFiConfig::WIFI_CONNECT_TIMEOUT_MS + 1);

    // Spec §6: baked-in over connect-timeout does NOT fall back to AP — it stays
    // CONNECTING (keeps retrying). AP fallback on connect-timeout is STORED_NVS-only.
    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTING));
    EXPECT_EQ(h.wifi.softApCalls, 0);
}

// ── Stage 4: retry on disconnect ─────────────────────────────────────────────

// Spec: after a disconnect from CONNECTED_STA, FirmwareApp transitions to
// RECONNECTING and re-attempts wifi.begin() with the stored credentials after
// the retry interval (WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS).
TEST(WiFiManagerWiringTest, RetriesBeginWithStoredCredsAfterDisconnectInterval) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.app->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.app->update(1000);
    ASSERT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_STA));

    const int beginsBeforeDisconnect = h.wifi.beginCalls;
    h.app->onWiFiDisconnected(WIFI_REASON_UNSPECIFIED);
    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::RECONNECTING));
    EXPECT_TRUE(h.app->shouldRestartTcpServer());

    // Mirror real hardware: the disconnect event fires BECAUSE the link dropped,
    // so status must read non-connected on the next tick. Leaving statusVal at
    // WL_CONNECTED would make the RECONNECTING handler see status==connected and
    // jump straight back to CONNECTED_STA (no re-begin) — masking the retry path.
    h.wifi.statusVal = WL_CONNECT_FAILED;

    // Shortly after disconnect: no retry yet (well under the retry interval, with
    // margin so the assertion does not depend on the exact lastRetryMs anchor).
    h.app->update(2000);
    EXPECT_EQ(h.wifi.beginCalls, beginsBeforeDisconnect);

    // Well past the retry interval: a re-begin with the stored creds must have
    // fired. Uses a comfortable margin (10s >> 5s interval) rather than pinning
    // the boundary, so the test is robust to the internal lastRetryMs semantics.
    h.app->update(10000);
    EXPECT_GT(h.wifi.beginCalls, beginsBeforeDisconnect);
    EXPECT_EQ(h.wifi.lastSsid, "net");
    EXPECT_EQ(h.wifi.lastPass, "pw");
}

// ── Stage 4: initial-connect timeout cap ────────────────────────────────────

// Spec §6: with STORED_NVS creds, past the initial-connect cap
// (WIFI_INITIAL_CONNECT_MAX_RETRIES * WIFI_CONNECT_RETRY_INTERVAL_MS = 300000ms)
// and no connection, fall back to AP mode (stop retrying STA).
TEST(WiFiManagerWiringTest, InitialConnectTimeoutStoredNvsFallsBackToAp) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.app->init();
    h.app->update(0);  // -> CONNECTING

    const uint32_t cap = WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                         WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    h.wifi.statusVal = WL_IDLE_STATUS;  // never connects
    h.app->update(cap + 1);

    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_AP));
    EXPECT_GE(h.wifi.softApCalls, 1);
}

// ── Stage 4: auth-failure disconnect goes straight to AP ─────────────────────

// Spec: an auth-failure disconnect reason is unrecoverable for STA — FirmwareApp
// must transition directly to AP mode (mirrors WiFiManager::onDisconnected).
TEST(WiFiManagerWiringTest, AuthFailureDisconnectFallsToApMode) {
    WiringHarness h;
    h.build();
    h.prefs.ssid = "net"; h.prefs.pass = "pw";
    h.app->init();
    h.wifi.statusVal = WL_CONNECTED;
    h.app->update(1000);
    ASSERT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_STA));

    h.app->onWiFiDisconnected(WIFI_REASON_AUTH_FAIL);
    EXPECT_EQ(h.app->getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_AP));
    EXPECT_FALSE(h.app->shouldRestartTcpServer());  // AP fallback does not restart TCP
}

} // namespace
