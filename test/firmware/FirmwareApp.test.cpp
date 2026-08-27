#include <gtest/gtest.h>
#include "firmware/vanilla/FirmwareApp.h"
#include "firmware/vanilla/IClientConnectionSource.h"
#include "firmware/vanilla/WiFiReasonCodes.h"
#include "firmware/vanilla/StatusLED.h"

#include <array>
#include <string>

using namespace esp32_firmware;

namespace {

// ── Fakes (reuse the seam interfaces FirmwareApp injects) ──────────────────────
class FakeWiFi : public IWiFi {
public:
    int mode = 0;
    std::string lastSsid, lastPass;
    int beginCalls = 0;
    int statusVal = 0;
    void setMode(int m) override { mode = m; }
    void begin(const char* ssid, const char* pass) override { lastSsid = ssid; lastPass = pass; ++beginCalls; }
    void disconnect(bool, bool) override {}
    int status() const override { return statusVal; }
    std::string localIP() const override { return "192.168.1.50"; }
    std::string softAPIP() const override { return "192.168.4.1"; }
    void softAP(const char*, const char*) override { mode = 2; }
    void setHostname(const char*) override {}
    int getMode() const override { return mode; }
    std::string SSID() const override { return lastSsid; }
    const char* disconnectReasonName(int) const override { return ""; }
    std::string BSSID() const override { return {}; }
    int8_t RSSI() const override { return 0; }
    void onEvent(std::function<void(int, WifiEventInfo*)>, int) override {}
};

class FakePreferences : public IPreferences {
public:
    std::string ssid, pass;
    int credCount = 0;
    void begin(const char*, bool) override {}
    void end() override {}
    size_t getBytesLength(const char* key) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_CRED_COUNT) == 0) return credCount > 0 ? 1 : 0;
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) return ssid.size();
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) return pass.size();
        return 0;
    }
    std::string getString(const char* key, const std::string& = "") override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) return ssid;
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) return pass;
        if (strcmp(key, WiFiConfig::NVS_WIFI_CRED_COUNT) == 0) return credCount > 0 ? "1" : "";
        return "";
    }
    size_t putString(const char* key, const std::string& value) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) ssid = value;
        else if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) pass = value;
        else if (strcmp(key, WiFiConfig::NVS_WIFI_CRED_COUNT) == 0) credCount = std::stoi(value);
        return value.size();
    }
    void clear() override { ssid.clear(); pass.clear(); credCount = 0; }
};

class FakeStatusLed : public IStatusLED {
public:
    int lastPattern = -1;
    int updateCalls = 0;
    void setPattern(int p) override { lastPattern = p; }
    void update(uint32_t) override { ++updateCalls; }
};

// Fake IClientConnectionSource: always reports no client connected.
class FakeClientConnectionSource : public IClientConnectionSource {
public:
    bool connected = false;
    bool isClientConnected() const override { return connected; }
};

class FakeWiFiDiscovery : public IWiFiDiscovery {
public:
    int mode = 1;
    int getMode() const override { return mode; }
    int status() const override { return 0; }
    std::string broadcastIP() const override { return "192.168.1.255"; }
};

class FakeUdp : public IUdp {
public:
    bool began = false;
    void begin(uint16_t) override { began = true; }
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
    bool inited = false;
    bool enabled() const override { return false; }
    void setOperatingMode(int) override {}
    void setServerName(int, const char*) override {}
    void setSyncMode(int) override {}
    void setSyncInterval(int32_t) override {}
    void setTimeSyncNotificationCallback(std::function<void(struct timeval*)>) override {}
    void init() override { inited = true; }
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

// Trivial CAN-adapter fakes. FirmwareApp's CAN path is exercised on-device, not
// asserted by this host suite, so these only need to satisfy the injected seams.
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

// No-op ISerial: WiFiManager's serial-trace contract is exercised by the
// dedicated WiFiManagerSerialTraceTest suite; these app-lifecycle tests are
// agnostic to trace output, so the fake discards it.
class FakeSerial : public ISerial {
public:
    void println(const char*) override {}

    __attribute__((format(printf, 2, 3)))
    void printf(const char*, ...) override {}
};

// Build a fully-wired FirmwareApp.
struct AppHarness {
    FakeWiFi wifi;
    FakePreferences prefs;
    FakeStatusLed led;
    FakeClientConnectionSource clientConnSource;
    FakeWiFiDiscovery wifiDisc;
    FakeUdp udp;
    FakeTime time;
    FakeSntp sntp;
    FakeTimeNtp timeNtp;
    FakeCanDriver canDriver;
    FakeTcpClient tcpClient;
    FakeSerialCan serialCan;
    FakeSerial serial;
    FirmwareApp app;

    AppHarness()
        : app(wifi, prefs, led, serial, wifiDisc, udp, time, sntp, timeNtp, kDeviceId,
              CanBridgeDeps{canDriver, tcpClient, serialCan},
              &clientConnSource) {}
};

// ── Init / lifecycle ─────────────────────────────────────────────────────────

// Removed: UpdateBeforeInitThrows - negative test for programmer error (assert handles it)

TEST(FirmwareAppTest, InitStartsWithNoStoredCredsLandsInApMode) {
    AppHarness h;  // empty prefs -> no creds
    h.app.init();
    h.app.update(0);  // first tick lazily opens discovery UDP + runs WiFi SM
    // No creds -> WiFiManager DISCONNECTED handler goes to AP mode (nothing was
    // ever configured).
    EXPECT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::WIFI_AP_MODE_DEFAULT));
}

TEST(FirmwareAppTest, InitOpensDiscoveryUdpOnFirstUpdateTickNotDuringInit) {
    AppHarness h;
    h.app.init();
    EXPECT_FALSE(h.udp.began);  // udp_.begin() deferred out of boot path
    h.app.update(0);
    EXPECT_TRUE(h.udp.began);   // opened on first loop tick
}

// ── Callback bridging ─────────────────────────────────────────────────────────

TEST(FirmwareAppTest, TcpRestartCallbackBridgesToFirmware) {
    AppHarness h;
    bool tcpRestarted = false;
    h.app.wifiManager().setTcpServerRestartCallback([&]() { tcpRestarted = true; });
    h.prefs.credCount = 1; h.prefs.ssid = "net"; h.prefs.pass = "pw";  // set creds BEFORE init
    h.app.init();

    // Drive WiFi to CONNECTED_STA (which fires WiFiManager's tcp-restart callback).
    h.wifi.statusVal = 3;  // WL_CONNECTED
    h.app.update(1000);
    EXPECT_TRUE(tcpRestarted);
}

TEST(FirmwareAppTest, BroadcastDiscoveryPolicyCountsBroadcasts) {
    AppHarness h;
    h.app.init();
    h.app.update(1000);  // now>=connectTimeMs so discovery interval gate passes
    // DiscoveryPolicy broadcasts on each interval; verify at least one occurred.
    EXPECT_GE(h.app.discoveryPolicy().broadcastCount(), 1);
}

// ── Credential pass-through ───────────────────────────────────────────────────

TEST(FirmwareAppTest, StoreAndHasAndClearCredentialsPassThrough) {
    AppHarness h;
    h.app.init();
    EXPECT_FALSE(h.app.wifiManager().hasStoredCredentials());
    EXPECT_TRUE(h.app.wifiManager().storeCredentials("net", "pw"));
    EXPECT_TRUE(h.app.wifiManager().hasStoredCredentials());
    std::string ssid, pass;
    EXPECT_TRUE(h.app.wifiManager().loadCredentials(ssid, pass));
    EXPECT_EQ(ssid, "net");
    EXPECT_EQ(pass, "pw");
    h.app.clear();
    EXPECT_FALSE(h.app.wifiManager().hasStoredCredentials());
}

TEST(FirmwareAppTest, FactoryResetClearsCredentials) {
    AppHarness h;
    h.app.init();
    h.app.wifiManager().storeCredentials("net", "pw");
    EXPECT_TRUE(h.app.wifiManager().factoryReset());
    EXPECT_FALSE(h.app.wifiManager().hasStoredCredentials());
}

// ── WiFi disconnect bridging ─────────────────────────────────────────────────

TEST(FirmwareAppTest, OnDisconnectedFromConnectedStaFlagsTcpRestart) {
    AppHarness h;
    h.prefs.credCount = 1; h.prefs.ssid = "net"; h.prefs.pass = "pw";  // set creds BEFORE init
    h.app.init();
    h.wifi.statusVal = 3;
    h.app.update(1000);  // -> CONNECTED_STA
    ASSERT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::WIFI_CONNECTED));

    h.app.onWiFiDisconnected(WIFI_REASON_UNSPECIFIED);
    EXPECT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::WIFI_CONNECTING));
    EXPECT_TRUE(h.app.tcpRestartPolicy().shouldRestart());
    h.app.tcpRestartPolicy().clear();
    EXPECT_FALSE(h.app.tcpRestartPolicy().shouldRestart());
}

TEST(FirmwareAppTest, OnDisconnectedAuthFailArmsCampaign_StaysConnecting) {
    // RESILIENT AUTH: a single AUTH_FAIL (202) must NOT bail to AP mode. It arms
    // a retry campaign and stays WIFI_CONNECTING. Model the radio dropping
    // (statusVal = 0 / WL_IDLE_STATUS) so the next update() drives the campaign.
    AppHarness h;
    h.prefs.credCount = 1; h.prefs.ssid = "net"; h.prefs.pass = "pw";  // set creds BEFORE init
    h.app.init();
    h.wifi.statusVal = 3;
    h.app.update(1000);  // -> CONNECTED_STA
    h.wifi.statusVal = 0;  // WL_IDLE_STATUS (radio dropped, not connected)
    h.app.onWiFiDisconnected(WIFI_REASON_AUTH_FAIL);
    h.app.update(2000);
    EXPECT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::WIFI_CONNECTING));
}

// ── LED animation driven each tick ────────────────────────────────────────────

TEST(FirmwareAppTest, UpdateDrivesStatusLedAnimationEveryTick) {
    AppHarness h;
    h.app.init();
    h.app.update(0);
    h.app.update(100);
    EXPECT_GE(h.led.updateCalls, 2);
}

// ── Own IP for the [STATE] heartbeat ──────────────────────────────────────────

TEST(FirmwareAppTest, GetOwnIpReturnsStaIpWhenNotAp) {
    // STA mode: stored credentials keep the manager on the station path
    // (setMode(1) at init), so the heartbeat reports the station address.
    AppHarness h;
    h.prefs.credCount = 1; h.prefs.ssid = "net"; h.prefs.pass = "pw";  // creds BEFORE init -> STA mode
    h.app.init();
    ASSERT_EQ(h.wifi.mode, 1);  // WIFI_STA, not AP
    EXPECT_EQ(h.app.wifi().localIP(), "192.168.1.50");  // FakeWiFi localIP
}

TEST(FirmwareAppTest, GetOwnIpReturnsSoftApIpInApMode) {
    // AP mode (no creds -> softAP on init): the heartbeat reports the soft-AP
    // address, which is the address a buddy must connect to.
    AppHarness h;  // empty prefs -> no creds
    h.app.init();
    h.app.update(0);
    ASSERT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::WIFI_AP_MODE_DEFAULT));
    EXPECT_EQ(h.app.wifi().softAPIP(), "192.168.4.1");  // FakeWiFi softAPIP
}

// ── TCP auth_fail does NOT drive the LED ──────────────────────────────────────
// A wrong AUTH token from a TCP client is the CLIENT's problem, not an ESP32
// error. The WiFi state (and therefore the LED, via selectLedPattern) must be
// unaffected; only the [EVENT] line reports it.

TEST(FirmwareAppTest, AuthFailed_DoesNotChangeLedPattern) {
    AppHarness h;
    h.app.init();
    h.app.update(0);  // settle to the normal pattern for the current WiFi state
    const int patternBefore = h.led.lastPattern;
    ASSERT_NE(patternBefore, -1);

    h.app.onAuthFailed("192.168.1.50");

    EXPECT_EQ(h.led.lastPattern, patternBefore)
        << "onAuthFailed must not touch the LED pattern";
}

} // namespace
