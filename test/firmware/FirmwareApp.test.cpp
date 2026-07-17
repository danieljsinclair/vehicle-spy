#include <gtest/gtest.h>
#include "firmware/vanilla/FirmwareApp.h"
#include "firmware/vanilla/WiFiReasonCodes.h"

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
    std::string localIP() const override { return "192.168.4.1"; }
    std::string softAPIP() const override { return "192.168.4.1"; }
    void softAP(const char*, const char*) override { mode = 2; }
    void setHostname(const char*) override {}
    int getMode() const override { return mode; }
    std::string SSID() const override { return lastSsid; }
    const char* disconnectReasonName(int) const override { return ""; }
    void onEvent(std::function<void(int, WifiEventInfo*)>, int) override {}
};

class FakePreferences : public IPreferences {
public:
    std::string ssid, pass;
    void begin(const char*, bool) override {}
    void end() override {}
    size_t getBytesLength(const char* key) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) return ssid.size();
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) return pass.size();
        return 0;
    }
    std::string getString(const char* key, const std::string& = "") override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) return ssid;
        if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) return pass;
        return "";
    }
    size_t putString(const char* key, const std::string& value) override {
        if (strcmp(key, WiFiConfig::NVS_WIFI_SSID) == 0) ssid = value;
        else if (strcmp(key, WiFiConfig::NVS_WIFI_PASS) == 0) pass = value;
        return value.size();
    }
    void clear() override { ssid.clear(); pass.clear(); }
};

class FakeStatusLed : public IStatusLED {
public:
    int lastPattern = -1;
    int updateCalls = 0;
    void setPattern(int p) override { lastPattern = p; }
    void update(uint32_t) override { ++updateCalls; }
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

// Build a fully-wired FirmwareApp.
struct AppHarness {
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
    FirmwareApp app;

    AppHarness()
        : app(wifi, prefs, led, wifiDisc, udp, time, sntp, timeNtp, kDeviceId,
              CanBridgeDeps{canDriver, tcpClient, serialCan}) {}
};

// ── Init / lifecycle ─────────────────────────────────────────────────────────

TEST(FirmwareAppTest, InitIsIdempotentGuarded) {
    AppHarness h;
    h.app.init();
    EXPECT_THROW(h.app.init(), std::logic_error);
}

TEST(FirmwareAppTest, UpdateBeforeInitThrows) {
    AppHarness h;
    EXPECT_THROW(h.app.update(0), std::logic_error);
}

TEST(FirmwareAppTest, InitStartsWithNoStoredCredsLandsInApMode) {
    AppHarness h;  // empty prefs -> no creds
    h.app.init();
    h.app.update(0);  // first tick lazily opens discovery UDP + runs WiFi SM
    // No creds -> WiFiManager DISCONNECTED handler goes to AP mode.
    EXPECT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_AP));
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
    h.app.setCallbacks(FirmwareCallbacks{.restartTcpServer = [&]() { tcpRestarted = true; }});
    h.prefs.ssid = "net"; h.prefs.pass = "pw";  // set creds BEFORE init
    h.app.init();

    // Drive WiFi to CONNECTED_STA (which fires WiFiManager's tcp-restart + ntp callbacks).
    h.wifi.statusVal = 3;  // WL_CONNECTED
    h.app.update(1000);
    EXPECT_TRUE(tcpRestarted);
}

TEST(FirmwareAppTest, BroadcastDiscoveryCallbackBridgesToFirmware) {
    AppHarness h;
    int broadcasts = 0;
    h.app.setCallbacks(FirmwareCallbacks{.broadcastDiscovery = [&]() { ++broadcasts; }});
    h.app.init();
    h.app.update(1000);  // now>=connectTimeMs so discovery interval gate passes
    // DiscoveryManager::broadcast() invokes the callback on each broadcast.
    EXPECT_GE(broadcasts, 1);
}

// ── Credential pass-through ───────────────────────────────────────────────────

TEST(FirmwareAppTest, StoreAndHasAndClearCredentialsPassThrough) {
    AppHarness h;
    h.app.init();
    EXPECT_FALSE(h.app.hasStoredCredentials());
    EXPECT_TRUE(h.app.storeCredentials("net", "pw"));
    EXPECT_TRUE(h.app.hasStoredCredentials());
    std::string ssid, pass;
    EXPECT_TRUE(h.app.loadCredentials(ssid, pass));
    EXPECT_EQ(ssid, "net");
    EXPECT_EQ(pass, "pw");
    EXPECT_TRUE(h.app.clearCredentials());
    EXPECT_FALSE(h.app.hasStoredCredentials());
}

TEST(FirmwareAppTest, FactoryResetClearsCredentials) {
    AppHarness h;
    h.app.init();
    h.app.storeCredentials("net", "pw");
    EXPECT_TRUE(h.app.factoryReset());
    EXPECT_FALSE(h.app.hasStoredCredentials());
}

// ── WiFi disconnect bridging ─────────────────────────────────────────────────

TEST(FirmwareAppTest, OnDisconnectedFromConnectedStaFlagsTcpRestart) {
    AppHarness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";  // set creds BEFORE init
    h.app.init();
    h.wifi.statusVal = 3;
    h.app.update(1000);  // -> CONNECTED_STA
    ASSERT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_STA));

    h.app.onWiFiDisconnected(WIFI_REASON_UNSPECIFIED);
    EXPECT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::RECONNECTING));
    EXPECT_TRUE(h.app.shouldRestartTcpServer());
    h.app.clearTcpServerRestartFlag();
    EXPECT_FALSE(h.app.shouldRestartTcpServer());
}

TEST(FirmwareAppTest, OnDisconnectedAuthFailGoesToApMode) {
    AppHarness h;
    h.prefs.ssid = "net"; h.prefs.pass = "pw";  // set creds BEFORE init
    h.app.init();
    h.wifi.statusVal = 3;
    h.app.update(1000);  // -> CONNECTED_STA
    h.app.onWiFiDisconnected(WIFI_REASON_AUTH_FAIL);
    EXPECT_EQ(h.app.getWiFiState(), static_cast<int>(WiFiState::State::CONNECTED_AP));
}

// ── LED animation driven each tick ────────────────────────────────────────────

TEST(FirmwareAppTest, UpdateDrivesStatusLedAnimationEveryTick) {
    AppHarness h;
    h.app.init();
    h.app.update(0);
    h.app.update(100);
    EXPECT_GE(h.led.updateCalls, 2);
}

} // namespace
