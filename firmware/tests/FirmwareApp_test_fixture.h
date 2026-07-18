#pragma once

// FirmwareApp_test_fixture.h - Shared test fixture, mocks, and AT spies for
// FirmwareApp host tests.
//
// Hoisted out of FirmwareApp_test.cpp so that additional test translation units
// (e.g. FirmwareApp_characterization_test.cpp) can reuse the same fixture, mocks,
// and boundary spies without duplicating them. Both TUs link into the single
// esp32-firmware-tests executable; the class definitions here are header-inline
// (no ODR violation) and carry no out-of-line member definitions.
//
// Testing PUBLIC CONTRACT only.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sys/time.h>
#include "vanilla/FirmwareApp.h"
#include "vanilla/CanBridge.h"
#include "mocks/WiFiMock.h"
#include "mocks/PreferencesMock.h"
#include "mocks/ArduinoMock.h"
#include "vanilla/DiscoveryManager.h"

namespace esp32_firmware {
namespace firmwareapp_test {

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::AtLeast;

// Mock IStatusLED for testing
class MockStatusLED : public IStatusLED {
public:
    MOCK_METHOD(void, setPattern, (int pattern), (override));
    MOCK_METHOD(void, update, (uint32_t now), (override));
};

// Mock IUdp for DiscoveryManager testing
class MockUdp : public IUdp {
public:
    MOCK_METHOD(void, begin, (uint16_t port), (override));
    MOCK_METHOD(int, beginPacket, (const std::string& ip, uint16_t port), (override));
    MOCK_METHOD(size_t, write, (const uint8_t* data, size_t len), (override));
    MOCK_METHOD(int, endPacket, (), (override));
};

// Mock ITime for DiscoveryManager testing
class MockTime : public ITime {
public:
    MOCK_METHOD(uint64_t, getCurrentTimestamp, (), (const, override));
    MOCK_METHOD(uint32_t, millis, (), (const, override));
};

// Mock ISntp for FirmwareApp NTP routing
class MockSntp : public ISntp {
public:
    MOCK_METHOD(bool, enabled, (), (const, override));
    MOCK_METHOD(void, setOperatingMode, (int mode), (override));
    MOCK_METHOD(void, setServerName, (int idx, const char* server), (override));
    MOCK_METHOD(void, setSyncMode, (int mode), (override));
    MOCK_METHOD(void, setSyncInterval, (int32_t intervalMs), (override));
    MOCK_METHOD(void, setTimeSyncNotificationCallback, (std::function<void(struct timeval*)> cb), (override));
    MOCK_METHOD(void, init, (), (override));
};

// Mock ITimeNtp for FirmwareApp NTP routing
class MockTimeNtp : public ITimeNtp {
public:
    MOCK_METHOD(void, setenv, (const char* name, const char* value, int overwrite), (override));
    MOCK_METHOD(void, tzset, (), (override));
    MOCK_METHOD(time_t, time, (time_t* t), (override));
    MOCK_METHOD(void, gmtime_r, (const time_t* timep, struct tm* result), (override));
    MOCK_METHOD(size_t, strftime, (char* s, size_t maxsize, const char* format, const struct tm* tm), (override));
};

// Trivial non-gmock stubs for the CanBridge adapters. FirmwareApp's tests do not
// assert the CAN stream path (that is the .ino's concern), so these only need to
// satisfy the injected interfaces without doing anything.
class StubCanDriver : public ICanDriver {
public:
    int driverInstall(CanGeneralConfig*, CanTimingConfig*, CanFilterConfig*) override { return 0; }
    int start() override { return 0; }
    int receive(CanFrame*, uint32_t) override { return -1; }  // no frames
};

class StubTcpClient : public ITcpClient {
public:
    bool connected() const override { return false; }
    size_t print(const char*) override { return 0; }
    void flush() override {}
};

class StubSerialCan : public ISerialCan {
public:
    size_t print(const char*) override { return 0; }
    void flush() override {}
};

// Minimal spies for the five AT runtime boundaries.
class SpyTcpClientAt : public ITcpClientAt {
public:
    std::string lastPrinted;
    int flushCalls = 0;
    void print(const char* str) override { lastPrinted = str ? str : ""; }
    void flush() override { ++flushCalls; }
};
class SpySerialAt : public ISerialAt {
public:
    std::string lastLine;
    int flushCalls = 0;
    void println(const char* str) override { lastLine = str ? str : ""; }
    void flush() override { ++flushCalls; }
};
class SpyEspAt : public IEspAt {
public:
    int restartCalls = 0;
    void restart() override { ++restartCalls; }
};
class SpyWifiStore : public IWifiCredentialStore {
public:
    bool nextResult = true;
    std::string lastSsid, lastPass;
    int storeCalls = 0;
    bool store(const std::string& ssid, const std::string& pass) override {
        ++storeCalls; lastSsid = ssid; lastPass = pass; return nextResult;
    }
};
class SpyMonitorState : public IMonitorState {
public:
    bool active = false;
    void setMonitorActive(bool a) override { active = a; }
};

class FirmwareAppTest : public ::testing::Test {
protected:
    WiFiMock wifiMock;
    PreferencesMock prefsMock;
    NiceMock<MockStatusLED> statusLedMock;
    NiceMock<MockUdp> udpMock;
    NiceMock<MockTime> timeMock;
    NiceMock<MockSntp> sntpMock;
    NiceMock<MockTimeNtp> timeNtpMock;
    StubCanDriver canDriverStub;
    StubTcpClient tcpClientStub;
    StubSerialCan serialStub;
    CanBridgeDeps canDeps{canDriverStub, tcpClientStub, serialStub};
    std::unique_ptr<FirmwareApp> firmwareApp;

    // Test device ID for DiscoveryManager
    std::array<uint8_t, 16> testDeviceId = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                             0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD,
                                             0xEE, 0xFF, 0x00};

    // Spy flags for callback verification
    bool restartTcpServerCalled = false;
    bool broadcastDiscoveryCalled = false;

    // Captured SNTP sync-notification callback (wired through ISntp by NtpTimeSync)
    std::function<void(struct timeval*)> capturedSyncCallback_;

    // Callback spies
    FirmwareCallbacks callbackSpies;

    void SetUp() override {
        wifiMock.reset();
        prefsMock.reset();
        arduino_mock::resetAllMocks();

        // Set WiFi to AP mode so DiscoveryManager will broadcast
        wifiMock.setMode(2);  // WIFI_AP mode

        // Initialize callback spies with capture lambdas
        callbackSpies.restartTcpServer = [this]() { restartTcpServerCalled = true; };
        callbackSpies.broadcastDiscovery = [this]() { broadcastDiscoveryCalled = true; };

        // Reset spy flags
        resetSpyFlags();
        capturedSyncCallback_ = nullptr;

        // Allow NiceMock leak (gmock quirk with NiceMock members)
        testing::Mock::AllowLeak(&udpMock);
        testing::Mock::AllowLeak(&timeMock);
        testing::Mock::AllowLeak(&sntpMock);
        testing::Mock::AllowLeak(&timeNtpMock);

        // Create FirmwareApp with all dependencies
        firmwareApp = createFirmwareApp("baked-ssid", "baked-pass");
    }

    void TearDown() override {
        firmwareApp.reset();
    }

    void resetSpyFlags() {
        restartTcpServerCalled = false;
        broadcastDiscoveryCalled = false;
    }

    // Helper to create FirmwareApp with all dependencies
    std::unique_ptr<FirmwareApp> createFirmwareApp(const char* bakedSsid = nullptr, const char* bakedPass = nullptr) {
        CanBridgeDeps canDeps{canDriverStub, tcpClientStub, serialStub};
        return std::make_unique<FirmwareApp>(
            wifiMock, prefsMock, statusLedMock,
            wifiMock,  // WiFiMock implements both IWiFi and IWiFiDiscovery
            udpMock, timeMock,
            sntpMock, timeNtpMock,
            testDeviceId,
            canDeps,
            bakedSsid, bakedPass
        );
    }
};

} // namespace firmwareapp_test
} // namespace esp32_firmware
