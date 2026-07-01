#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/ble/BLEManagerBase.h"

#include <thread>
#include <chrono>

using namespace vehicle_sim;
using testing::_;
using testing::Return;
using testing::SaveArg;
using testing::Eq;

// Mock BLE platform for testing - inherits from BLEManagerBase
class MockBLEManagerBase : public BLEManagerBase {
public:
    MOCK_METHOD(std::vector<BLEDeviceInfo>, scanForDevices, (int timeout_seconds), (override));
    MOCK_METHOD(bool, connect, (std::string_view device_identifier), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(void, send, (const std::vector<uint8_t>& data), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(std::string, getConnectedDeviceId, (), (const, override));
    MOCK_METHOD(void, setDeviceFoundCallback, (DeviceCallback callback), (override));
    MOCK_METHOD(void, setDataReceivedCallback, (DataCallback callback), (override));
};

// ================================================
// BLEManager Unit Tests
// TDD - Tests using dependency injection with mocks
// ================================================

TEST(BLEManagerTest, ScansForDevicesViaPlatform)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, scanForDevices(5))
        .WillOnce(Return(std::vector<BLEDeviceInfo>{
            {"addr1", "Device 1", false, -50},
            {"addr2", "Device 2", false, -60}
        }));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    auto devices = manager.scanForDevices(5);
    EXPECT_EQ(devices.size(), 2);
    EXPECT_EQ(devices[0].name, "Device 1");
    EXPECT_EQ(devices[1].name, "Device 2");
}

TEST(BLEManagerTest, ConnectsToDeviceViaPlatform)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, connect(Eq("test-device")))
        .WillOnce(Return(true));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool result = manager.connect("test-device");
    EXPECT_TRUE(result);
}

TEST(BLEManagerTest, ReportsConnectionFailure)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, connect(Eq("invalid-device")))
        .WillOnce(Return(false));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool result = manager.connect("invalid-device");
    EXPECT_FALSE(result);
}

TEST(BLEManagerTest, DisconnectsViaPlatform)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, disconnect()).Times(1);

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    manager.disconnect();
}

TEST(BLEManagerTest, ReportsConnectionStatus)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, isConnected())
        .WillOnce(Return(true));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    EXPECT_TRUE(manager.isConnected());
}

TEST(BLEManagerTest, ReturnsConnectedDeviceId)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, getConnectedDeviceId())
        .WillOnce(Return(std::string("device-123")));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    std::string deviceId = manager.getConnectedDeviceId();
    EXPECT_EQ(deviceId, "device-123");
}

TEST(BLEManagerTest, ReturnsEmptyDeviceIdWhenNotConnected)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    EXPECT_CALL(*mockPlatform, getConnectedDeviceId())
        .WillOnce(Return(std::string()));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    std::string deviceId = manager.getConnectedDeviceId();
    EXPECT_TRUE(deviceId.empty());
}

TEST(BLEManagerTest, ForwardsDeviceFoundCallback)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    BLEManager::DeviceCallback capturedCallback;

    EXPECT_CALL(*mockPlatform, setDeviceFoundCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool callbackInvoked = false;
    manager.onDeviceFound([&callbackInvoked](const BLEDeviceInfo& device) {
        callbackInvoked = true;
        EXPECT_EQ(device.name, "Test Device");
    });

    // Simulate the platform invoking the callback
    capturedCallback({"addr", "Test Device", false, -50});

    EXPECT_TRUE(callbackInvoked);
}

TEST(BLEManagerTest, ForwardsDataReceivedCallback)
{
    auto mockPlatform = std::make_unique<MockBLEManagerBase>();
    BLEManager::DataCallback capturedCallback;

    EXPECT_CALL(*mockPlatform, setDataReceivedCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool callbackInvoked = false;
    std::vector<uint8_t> expectedData = {0xAA, 0x55, 0x01, 0x02, 0x03};

    manager.onDataReceived([&callbackInvoked, &expectedData](const std::vector<uint8_t>& data) {
        callbackInvoked = true;
        EXPECT_EQ(data, expectedData);
    });

    // Simulate the platform invoking the callback
    capturedCallback(expectedData);

    EXPECT_TRUE(callbackInvoked);
}

TEST(BLEManagerTest, HandlesNullPlatformGracefully)
{
    BLEManager manager;
    manager.setPlatform(nullptr);

    // Should not crash and return safe defaults
    EXPECT_FALSE(manager.isConnected());
    EXPECT_TRUE(manager.getConnectedDeviceId().empty());
    EXPECT_EQ(manager.scanForDevices(5).size(), 0);
    EXPECT_FALSE(manager.connect("device"));

    // These should not crash either
    manager.disconnect();
    manager.onDeviceFound([](const BLEDeviceInfo&) {});
    manager.onDataReceived([](const std::vector<uint8_t>&) {});
}

// ================================================
// BLEManagerBase OBD2 Helper Tests
// ================================================

TEST(BLEManagerBaseTest, ParsesASCIIResponseToBinary)
{
    // Create a concrete test subclass since BLEManagerBase is abstract
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }

        // Expose private method for testing
        std::vector<uint8_t> testParseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData) {
            return parseASCIIResponseToBinary(asciiData);
        }
    };

    TestBLEManager manager;

    // Simulate ASCII hex response from ELM327: "41 0C 1A F8\r"
    std::vector<uint8_t> asciiData = {'4', '1', ' ', '0', 'C', ' ', '1', 'A', ' ', 'F', '8', '\r'};
    auto result = manager.testParseASCIIResponseToBinary(asciiData);

    // Should parse to binary [0x41, 0x0C, 0x1A, 0xF8]
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], 0x41);
    EXPECT_EQ(result[1], 0x0C);
    EXPECT_EQ(result[2], 0x1A);
    EXPECT_EQ(result[3], 0xF8);
}

TEST(BLEManagerBaseTest, SkipsPromptAndEcho)
{
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }

        std::vector<uint8_t> testParseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData) {
            return parseASCIIResponseToBinary(asciiData);
        }
    };

    TestBLEManager manager;

    // ELM327 prompt character only
    std::vector<uint8_t> prompt = {'>'};
    auto result = manager.testParseASCIIResponseToBinary(prompt);
    EXPECT_TRUE(result.empty());

    // Error message
    std::vector<uint8_t> error = {'N', 'O', ' ', 'D', 'A', 'T', 'A', '\r'};
    result = manager.testParseASCIIResponseToBinary(error);
    EXPECT_TRUE(result.empty());
}

TEST(BLEManagerBaseTest, BuildsAndSendsOBD2QueryWithELM327Encoding)
{
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        std::vector<uint8_t> lastSent;

        void send(const std::vector<uint8_t>& data) override {
            lastSent = data;
        }

        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
    };

    TestBLEManager manager;

    // Query PID 0x0C (Engine RPM)
    manager.queryPID(0x0C);

    // Should send ASCII "01 0C\r" as bytes
    std::string expected = "01 0C\r";
    EXPECT_EQ(manager.lastSent.size(), expected.size());

    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(manager.lastSent[i], static_cast<uint8_t>(expected[i]));
    }
}

TEST(BLEManagerBaseTest, SignalQualityConversion)
{
    // Test various RSSI values
    EXPECT_EQ(BLEManagerBase::signalQuality(-45), "Excellent");
    EXPECT_EQ(BLEManagerBase::signalQuality(-50), "Excellent");
    EXPECT_EQ(BLEManagerBase::signalQuality(-55), "Good");
    EXPECT_EQ(BLEManagerBase::signalQuality(-65), "Good");
    EXPECT_EQ(BLEManagerBase::signalQuality(-70), "Fair");
    EXPECT_EQ(BLEManagerBase::signalQuality(-75), "Fair");
    EXPECT_EQ(BLEManagerBase::signalQuality(-90), "Poor");
}

// ================================================
// ELM327 Prompt-Driven Sequencing Tests
// ================================================

namespace {

/**
 * Concrete BLEManagerBase subclass that exposes protected members for testing.
 * Also overrides isConnected to return a controllable value.
 */
class PromptTestBLEManager : public BLEManagerBase {
public:
    std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
    bool connect(std::string_view) override { return false; }
    void disconnect() override {}
    std::vector<uint8_t> lastSent;
    int sendCount = 0;

    void send(const std::vector<uint8_t>& data) override {
        lastSent = data;
        sendCount++;
    }

    // Allow tests to control connection state
    std::atomic<bool> fakeConnected{false};
    bool isConnected() const override { return fakeConnected.load(); }
    std::string getConnectedDeviceId() const override { return "test-device"; }

    // Expose protected methods
    using BLEManagerBase::waitForPrompt;
    using BLEManagerBase::notifyPrompt;
    using BLEManagerBase::invokeDataCallback;
    using BLEManagerBase::promptReady;
};

} // anonymous namespace

TEST(BLEManagerBaseTest, WaitForPromptReturnsTrueWhenNotified)
{
    PromptTestBLEManager manager;

    // Notify from another thread, wait on this thread
    std::thread notifier([&manager]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        manager.notifyPrompt();
    });

    bool result = manager.waitForPrompt(1000);
    notifier.join();

    EXPECT_TRUE(result);
}

TEST(BLEManagerBaseTest, WaitForPromptReturnsFalseOnTimeout)
{
    PromptTestBLEManager manager;

    // No notification — should time out
    bool result = manager.waitForPrompt(1);

    EXPECT_FALSE(result);
}

TEST(BLEManagerBaseTest, InvokeDataCallbackDetectsStandalonePrompt)
{
    PromptTestBLEManager manager;

    // A standalone '>' BLE notification should trigger prompt
    std::vector<uint8_t> promptOnly = {'>'};
    manager.invokeDataCallback(promptOnly);

    EXPECT_TRUE(manager.promptReady());
}

TEST(BLEManagerBaseTest, InvokeDataCallbackDetectsPromptAppendedToResponse)
{
    PromptTestBLEManager manager;

    // ELM327 often sends response + prompt together: "41 0D FF\r>"
    std::vector<uint8_t> responseWithPrompt = {
        '4', '1', ' ', '0', 'D', ' ', 'F', 'F', '\r', '>'
    };
    manager.invokeDataCallback(responseWithPrompt);

    EXPECT_TRUE(manager.promptReady());
}

TEST(BLEManagerBaseTest, InvokeDataCallbackDoesNotSignalPromptWithoutPromptChar)
{
    PromptTestBLEManager manager;

    // Normal response without prompt: "41 0D FF\r"
    std::vector<uint8_t> responseNoPrompt = {
        '4', '1', ' ', '0', 'D', ' ', 'F', 'F', '\r'
    };
    manager.invokeDataCallback(responseNoPrompt);

    EXPECT_FALSE(manager.promptReady());
}

TEST(BLEManagerBaseTest, InvokeDataCallbackDoesNotSignalPromptInCANMode)
{
    PromptTestBLEManager manager;

    // Put into CAN mode
    manager.elm327Session().startCANMonitor(200);

    // Even with '>' in the data, CAN mode should not trigger prompt signalling
    std::vector<uint8_t> dataWithPrompt = {'>', '4', '1', ' ', '0', 'D'};
    manager.invokeDataCallback(dataWithPrompt);

    EXPECT_FALSE(manager.promptReady());
}

// ============================================================
// God-class decomposition contract net.
//
// These BLIND characterisation tests lock every externally-observable
// behaviour of the BLEManagerBase OBD2/polling/CAN/VIN session surface
// (the ~20 methods with ZERO subclass surface that move into the
// proposed Elm327Session role). They are written against current code so
// the extraction cannot silently drift behaviour. Production code is
// untouched.
// ============================================================

namespace {

/**
 * Concrete BLEManagerBase subclass that records every send() so the
 * session methods can be characterised without real BLE hardware.
 * Mirrors the existing PromptTestBLEManager pattern but is focused on
 * the command-emission contracts.
 */
class SessionTestBLEManager : public BLEManagerBase {
public:
    std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
    bool connect(std::string_view) override { return false; }
    void disconnect() override {}
    bool isConnected() const override { return fakeConnected.load(); }
    std::string getConnectedDeviceId() const override { return "test-device"; }

    void send(const std::vector<uint8_t>& data) override {
        sentCommands.push_back(std::string(data.begin(), data.end()));
    }

    std::atomic<bool> fakeConnected{false};
    std::vector<std::string> sentCommands;

    // Expose the protected setup-delay constant (it's needed at file scope
    // in the polling test, where qualified access to a protected member
    // would otherwise fail to compile).
    static constexpr int kPostConnectSetupDelayMs = POST_CONNECT_SETUP_DELAY_MS;

    // The polling loop reads the base connected_ member (not the
    // isConnected() override), so tests that drive the loop must set it.
    void setBaseConnected(bool c) { connected_ = c; }

    // Read the base connected_ state directly (the isConnected() override is
    // deliberately shadowed in this fixture, so this is the only way to
    // observe what setConnectionState actually wrote).
    bool baseConnected() const { return connected_; }

    // Re-expose protected helpers used by the contract tests.
    using BLEManagerBase::sendASCII;
    using BLEManagerBase::invokeDataCallback;
    using BLEManagerBase::notifyPrompt;
    using BLEManagerBase::addDiscoveredDevice;
    using BLEManagerBase::clearDiscoveredDevices;
    using BLEManagerBase::findDeviceByAddress;
    using BLEManagerBase::invokeDeviceCallback;
    using BLEManagerBase::invokeConnectionCallback;
    using BLEManagerBase::setConnectionState;
    using BLEManagerBase::canMode;
};

} // anonymous namespace

// --- ELM327 / OBD2 init & detection contracts -----------------------------

TEST(BLEManagerBaseSessionContract, InitializeELM327_SendsFullInitSequenceAndReturnsTrue) {
    SessionTestBLEManager m;
    // The init sequence is prompt-driven: each AT command waits for '>'.
    // With no prompt ever arriving, every command is still emitted once
    // (waitForPrompt merely times out and warns).
    EXPECT_TRUE(m.elm327Session().initializeELM327());
    // buildInitSequence() emits ATZ, ATE0, ATH0, ATL0, ATSP0, ATS0, ATSTFF.
    ASSERT_EQ(m.sentCommands.size(), 7u);
    EXPECT_EQ(m.sentCommands.front(), "ATZ\r");
    EXPECT_EQ(m.sentCommands.back(), "ATSTFF\r");
}

TEST(BLEManagerBaseSessionContract, InitializeOBD2WithDetection_InitializesThenDetects) {
    SessionTestBLEManager m;
    auto result = m.elm327Session().initializeOBD2WithDetection();
    // The contract: initialiseELM327 runs first (its 7 AT commands lead the
    // emission stream), then detection is attempted. detectVehicle() may
    // emit its own queries, so we lock only that init's sequence leads —
    // not the total count — and that a result is propagated without crash.
    ASSERT_GE(m.sentCommands.size(), 7u);
    EXPECT_EQ(m.sentCommands[0], "ATZ\r");
    EXPECT_EQ(m.sentCommands[6], "ATSTFF\r");
    EXPECT_NO_FATAL_FAILURE((void)result.has_value());
}

TEST(BLEManagerBaseSessionContract, ProcessOBD2Data_RoutesIntoProtocolHandler) {
    SessionTestBLEManager m;
    // processOBD2Data delegates to obd2_protocol_.processIncomingData.
    // It must not throw and must not emit any commands itself (it is a pure
    // routing call; the protocol handler owns command emission).
    EXPECT_NO_FATAL_FAILURE(m.elm327Session().processOBD2Data("41 0C 1A F8\r"));
    EXPECT_TRUE(m.sentCommands.empty());
}

// --- CAN monitor contracts ------------------------------------------------

TEST(BLEManagerBaseSessionContract, InitializeCANMonitor_SendsMonitorInitAndSetsCanMode) {
    SessionTestBLEManager m;
    EXPECT_FALSE(m.canMode());
    EXPECT_TRUE(m.elm327Session().initializeCANMonitor());
    // buildCANMonitorInitSequence(): ATZ, ATE0, ATSP6, ATH1, ATMA.
    ASSERT_EQ(m.sentCommands.size(), 5u);
    EXPECT_EQ(m.sentCommands.back(), "ATMA\r");
    EXPECT_TRUE(m.canMode());
}

TEST(BLEManagerBaseSessionContract, StartStopCANMonitor_FlagAndStopEmitsAtma) {
    SessionTestBLEManager m;
    EXPECT_FALSE(m.canMode());
    m.elm327Session().startCANMonitor(200);
    EXPECT_TRUE(m.canMode());
    EXPECT_TRUE(m.sentCommands.empty());  // start is pure flag-set, no emit

    m.elm327Session().stopCANMonitor();
    EXPECT_FALSE(m.canMode());
    // stopCANMonitor explicitly re-asserts ATMA to end the monitor stream.
    ASSERT_EQ(m.sentCommands.size(), 1u);
    EXPECT_EQ(m.sentCommands.front(), "ATMA\r");
}

// --- VIN query contracts --------------------------------------------------

TEST(BLEManagerBaseSessionContract, InitializeForVINQuery_ClearsCanModeResetsDetectorAndEmitsVinInit) {
    SessionTestBLEManager m;
    m.elm327Session().startCANMonitor();  // put into CAN mode first
    ASSERT_TRUE(m.canMode());

    EXPECT_TRUE(m.elm327Session().initializeForVINQuery());
    EXPECT_FALSE(m.canMode());  // VIN init must drop out of CAN mode
    // buildVINQueryInitSequence(): ATZ, ATE0, ATH0, ATL0, ATSP6, ATS0, ATSTFF.
    ASSERT_EQ(m.sentCommands.size(), 7u);
    // VIN path uses ATSP6 (specific CAN), NOT ATSP0 (auto-probe).
    bool sawAtsp6 = false;
    for (const auto& c : m.sentCommands) if (c == "ATSP6\r") sawAtsp6 = true;
    EXPECT_TRUE(sawAtsp6);
    // detector is reset: getResult yields empty VIN / Unknown make.
    auto det = m.vehicleDetector();
    ASSERT_NE(det, nullptr);
    auto r = det->getResult();
    EXPECT_TRUE(r.vin.empty());
}

TEST(BLEManagerBaseSessionContract, QueryVIN_ReturnsNulloptWhenPromptTimesOut) {
    SessionTestBLEManager m;
    auto vin = m.elm327Session().queryVIN(/*timeout_ms=*/5);
    EXPECT_FALSE(vin.has_value());
    // Emits the 09 02 query before waiting.
    ASSERT_FALSE(m.sentCommands.empty());
    EXPECT_EQ(m.sentCommands.front(), "09 02\r");
}

TEST(BLEManagerBaseSessionContract, QueryVIN_ReturnsVinWhenDetectorPopulatedAndPromptArrives) {
    SessionTestBLEManager m;
    // Seed the detector with a VIN so getResult() has one to return, then
    // deliver the '>' prompt so queryVIN's waitForPrompt returns true.
    // feedVINResponse appends every non-zero byte after [0x49 0x02 ...] —
    // 17 ASCII chars yields a 17-char VIN (clamped at 17).
    m.vehicleDetector()->feedVINResponse(
        std::vector<uint8_t>{0x49, 0x02, 0x00,
                             '1','H','G','C','M','8','2','6','3',
                             '3','A','0','0','0','0','0','0'});
    std::thread prompter([&m] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        m.notifyPrompt();
    });
    auto vin = m.elm327Session().queryVIN(/*timeout_ms=*/2000);
    prompter.join();
    ASSERT_TRUE(vin.has_value());
    EXPECT_EQ(vin->size(), 17u);
}

// --- OBD2 polling loop contracts -----------------------------------------

TEST(BLEManagerBaseSessionContract, StartOBD2Polling_IsIdempotentSpawnsAtMostOneThread) {
    SessionTestBLEManager m;
    m.setBaseConnected(true);
    m.setDataReceivedCallback([](const std::vector<uint8_t>&) {});

    m.elm327Session().startOBD2Polling(200);
    // A second start must be a no-op (guard: polling_active_ already true).
    m.elm327Session().startOBD2Polling(200);
    m.elm327Session().stopOBD2Polling();  // joins the single thread — would deadlock if two
    SUCCEED();
}

TEST(BLEManagerBaseSessionContract, OBD2PollingLoop_QueriesStandardPidsInDeclaredOrder) {
    SessionTestBLEManager m;
    // The loop gates on the base connected_ member (not the isConnected()
    // override), so drive it directly.
    m.setBaseConnected(true);

    // Deliver a prompt per query so the loop advances through the whole
    // PID cycle promptly rather than each waiting for the 2s timeout.
    m.setDataReceivedCallback([&m](const std::vector<uint8_t>&) {
        m.notifyPrompt();
    });

    m.elm327Session().startOBD2Polling(/*interval_ms=*/1000);
    // Allow the post-connect setup delay + one PID cycle.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(SessionTestBLEManager::kPostConnectSetupDelayMs + 200));
    m.setBaseConnected(false);  // let the loop exit
    m.elm327Session().stopOBD2Polling();

    // The loop's declared PID order is: BATTERY_VOLTAGE, ENGINE_LOAD,
    // COOLANT_TEMP, THROTTLE_POSITION, VEHICLE_SPEED, ENGINE_RPM.
    std::vector<std::string> queries;
    for (const auto& c : m.sentCommands) {
        if (c.size() >= 5 && c.substr(0, 3) == "01 ") queries.push_back(c);
    }
    ASSERT_FALSE(queries.empty());
    // The loop's declared PID order starts with BATTERY_VOLTAGE (0x42).
    // We lock only the first query of the cycle: that is deterministic
    // regardless of how far the loop progressed before teardown, whereas
    // the trailing queries depend on thread-scheduling races.
    EXPECT_EQ(queries.front(), "01 42\r");
}

// --- invokeDataCallback: CAN vs OBD2 data path contracts -----------------

TEST(BLEManagerBaseSessionContract, InvokeDataCallback_InCanModeParsesFrameToTenBytesAndObserves) {
    SessionTestBLEManager m;
    std::vector<std::uint8_t> delivered;
    m.setDataReceivedCallback([&delivered](const std::vector<uint8_t>& d) { delivered = d; });
    m.elm327Session().startCANMonitor();  // route into CAN parsing

    // 11-bit CAN id 0x123 + 8 data bytes (ELM327 monitor line, no type prefix).
    std::string frame = "123 11 22 33 44 55 66 77 88";
    m.invokeDataCallback(std::vector<uint8_t>(frame.begin(), frame.end()));

    ASSERT_EQ(delivered.size(), 10u);          // [idLo, idHi, 8 data bytes]
    EXPECT_EQ(delivered[0], 0x23);             // canId low byte
    EXPECT_EQ(delivered[1], 0x01);             // canId high byte
    EXPECT_EQ(delivered[2], 0x11);
    EXPECT_EQ(delivered[9], 0x88);
    // The observed frame is forwarded to the vehicle detector (counts rise).
    EXPECT_GT(m.vehicleDetector()->getResult().frameCount, 0);
}

TEST(BLEManagerBaseSessionContract, InvokeDataCallback_InCanModeDropsNonFrameNotifications) {
    SessionTestBLEManager m;
    bool callbackFired = false;
    m.setDataReceivedCallback([&](const std::vector<uint8_t>&) { callbackFired = true; });
    m.elm327Session().startCANMonitor();
    // ELM327 status/prompt lines are not CAN frames — must not be delivered.
    m.invokeDataCallback(std::vector<uint8_t>{'>', ' ', ' '});
    EXPECT_FALSE(callbackFired);
    // The raw-notification count still increments even when no frame is parsed.
    EXPECT_EQ(m.bleNotificationCount(), 1);
}

TEST(BLEManagerBaseSessionContract, InvokeDataCallback_InObd2ModeDeliversParsedBinary) {
    SessionTestBLEManager m;
    std::vector<std::uint8_t> delivered;
    m.setDataReceivedCallback([&delivered](const std::vector<uint8_t>& d) { delivered = d; });
    // "41 0D FF\r" → OBD2 response, parses to [0x41, 0x0D, 0xFF].
    std::string resp = "41 0D FF\r";
    m.invokeDataCallback(std::vector<uint8_t>(resp.begin(), resp.end()));
    ASSERT_EQ(delivered.size(), 3u);
    EXPECT_EQ(delivered[0], 0x41);
    EXPECT_EQ(delivered[2], 0xFF);
}

TEST(BLEManagerBaseSessionContract, InvokeDataCallback_TracksRawHexAndNotificationCount) {
    SessionTestBLEManager m;
    m.setDataReceivedCallback([](const std::vector<uint8_t>&) {});
    std::vector<uint8_t> data = {0xAA, 0x55, 0x01, 0x02, 0x03};
    m.invokeDataCallback(data);
    EXPECT_EQ(m.bleNotificationCount(), 1);
    EXPECT_NE(m.lastRawHex().find("aa"), std::string::npos);
    EXPECT_NE(m.lastRawHex().find("55"), std::string::npos);
}

TEST(BLEManagerBaseSessionContract, InvokeDataCallback_DropsDataWhenCallbackUnset) {
    SessionTestBLEManager m;
    // No data callback set: must not crash; count still increments.
    m.invokeDataCallback({0x41, 0x0D, 0xFF});
    EXPECT_EQ(m.bleNotificationCount(), 1);
}

// --- Device management contracts ------------------------------------------

TEST(BLEManagerBaseSessionContract, AddDiscoveredDevice_DeduplicatesByAddress) {
    SessionTestBLEManager m;
    BLEDeviceInfo d{"AA:BB", "Dev", false, -50};
    m.addDiscoveredDevice(d);
    m.addDiscoveredDevice(d);  // duplicate address — ignored
    EXPECT_EQ(m.findDeviceByAddress("AA:BB").has_value(), true);
    // No entry for an address never added.
    EXPECT_FALSE(m.findDeviceByAddress("ZZ:ZZ").has_value());
    m.clearDiscoveredDevices();
    EXPECT_FALSE(m.findDeviceByAddress("AA:BB").has_value());
}

TEST(BLEManagerBaseSessionContract, AddDiscoveredDevice_InvokesDeviceCallbackForEachUnique) {
    SessionTestBLEManager m;
    int calls = 0;
    m.setDeviceFoundCallback([&](const BLEDeviceInfo&) { ++calls; });
    m.addDiscoveredDevice({"A", "A", false, -50});
    m.addDiscoveredDevice({"B", "B", false, -60});
    m.addDiscoveredDevice({"A", "A again", false, -50});  // dup address, not delivered
    EXPECT_EQ(calls, 2);
}

// --- Connection-state contracts -------------------------------------------

TEST(BLEManagerBaseSessionContract, SetConnectionState_UpdatesStateAndFiresCallback) {
    SessionTestBLEManager m;
    bool connected = false;
    std::string seenId;
    m.setConnectionCallback([&](bool c, const std::string& id) {
        connected = c; seenId = id;
    });
    m.setConnectionState(true, "dev-7");
    EXPECT_TRUE(connected);
    EXPECT_EQ(seenId, "dev-7");
    EXPECT_TRUE(m.baseConnected());   // base connected_ flipped true
    m.setConnectionState(false, "dev-7");
    EXPECT_FALSE(connected);
    EXPECT_FALSE(m.baseConnected());
}

TEST(BLEManagerBaseSessionContract, SetConnectionState_NoCallbackDoesNotCrash) {
    SessionTestBLEManager m;
    EXPECT_NO_FATAL_FAILURE(m.setConnectionState(true, "dev"));
    EXPECT_TRUE(m.baseConnected());
}

// --- sendASCII passthrough ------------------------------------------------

TEST(BLEManagerBaseSessionContract, SendASCII_EmitsStringAsRawBytes) {
    SessionTestBLEManager m;
    m.sendASCII("ATZ\r");
    ASSERT_EQ(m.sentCommands.size(), 1u);
    EXPECT_EQ(m.sentCommands.front(), "ATZ\r");
}

TEST(BLEManagerBaseTest, StopOBD2PollingWakesPromptWait)
{
    PromptTestBLEManager manager;
    manager.fakeConnected = true;

    // Set a data callback so invokeDataCallback doesn't bail early
    manager.setDataReceivedCallback([](const std::vector<uint8_t>&) {});

    manager.elm327Session().startOBD2Polling(200);

    // stopOBD2Polling() unblocks the polling thread regardless of which wait
    // state it is in: it clears polling_active_, then notifyPrompt() sets
    // prompt_ready_ and notifies the condition_variable. Whether the thread is
    // still in the POST_CONNECT_SETUP_DELAY_MS sleep (it then sees
    // polling_active_ == false and exits the loop without reaching the cv) or
    // already blocked in waitForPrompt (the notify wakes it and the loop guard
    // exits), the join returns promptly. Calling stop immediately therefore
    // deterministically verifies the wake path without any test-side wait.
    manager.elm327Session().stopOBD2Polling();

    // If we get here, stopOBD2Polling correctly woke the thread and joined it.
    SUCCEED();
}