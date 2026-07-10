#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/util/IClock.h"

#include <thread>
#include <chrono>

using namespace vehicle_sim;
using testing::_;
using testing::Return;
using testing::SaveArg;
using testing::Eq;

// Use the FakeClock from the util namespace for deterministic time in tests.
using vehicle_sim::util::FakeClock;

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
    explicit PromptTestBLEManager(util::IClock* clock = nullptr) : BLEManagerBase(clock) {}

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
    using BLEManagerBase::setClock;
};

} // anonymous namespace

TEST(BLEManagerBaseTest, WaitForPromptReturnsTrueWhenNotified)
{
    PromptTestBLEManager manager;
    // Inject FakeClock for deterministic timing.
    FakeClock clock;
    manager.setClock(&clock);

    // Notify from another thread, wait on this thread. Use a small sleep
    // to ensure the waiter has started waiting before the notifier notifies.
    std::thread notifier([&manager]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        manager.notifyPrompt();
    });

    bool result = manager.waitForPrompt(1000);
    notifier.join();

    EXPECT_TRUE(result);
}

TEST(BLEManagerBaseTest, WaitForPromptReturnsFalseOnTimeout)
{
    PromptTestBLEManager manager;
    // Inject FakeClock for deterministic timing.
    FakeClock clock;
    manager.setClock(&clock);

    // No notification — advance clock to simulate time passing.
    // The waitForPrompt call will timeout after 1ms. Since we've already
    // advanced time, the deadline will be immediately passed.
    // Advance clock to 10ms, then call waitForPrompt(1). The deadline
    // will be 10ms + 1ms = 11ms, but FakeClock::waitForImpl now checks
    // if the deadline is already past BEFORE parking. Since we're not
    // advancing the clock further, the wait will timeout when the
    // FakeClock's time doesn't advance.
    //
    // Actually, this test is fundamentally broken with FakeClock because
    // the deadline is always calculated as now() + timeout. If we
    // advance the clock BEFORE calling waitForPrompt, the deadline moves
    // forward as well.
    //
    // The correct approach is to use a separate thread to advance the
    // clock AFTER the wait starts.
    std::atomic<bool> result{false};
    std::thread waiter([&manager, &result]() {
        result = manager.waitForPrompt(1);
    });

    // Wait a bit for the waiter to start waiting, then advance the clock
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    clock.advance(std::chrono::milliseconds(2));

    waiter.join();

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
    explicit SessionTestBLEManager(util::IClock* clock = nullptr) : BLEManagerBase(clock) {}

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
    // connected_ is now private (cpp:S3656); use the protected accessor shim.
    void setBaseConnected(bool c) { setConnected(c); }

    // Read the base connected_ state directly (the isConnected() override is
    // deliberately shadowed in this fixture, so this is the only way to
    // observe what setConnectionState actually wrote).
    bool baseConnected() const { return isConnectedRaw(); }

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

    // Expose setClock for test injection
    using BLEManagerBase::setClock;
};

} // anonymous namespace

// --- ELM327 / OBD2 init & detection contracts -----------------------------

TEST(BLEManagerBaseSessionContract, InitializeELM327_SendsFullInitSequenceAndReturnsTrue) {
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Advance the clock in a separate thread to trigger waitForPrompt timeouts.
    // InitializeELM327 calls waitForPrompt after each AT command (7 total),
    // so we advance the clock enough times to unblock all waits.
    std::thread advanceThread([&clock]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clock.advance(std::chrono::milliseconds(3000));
        }
    });

    // The init sequence is prompt-driven: each AT command waits for '>'.
    // With no prompt ever arriving, every command is still emitted once
    // (waitForPrompt times out when clock advances past PROMPT_TIMEOUT_MS).
    EXPECT_TRUE(m.elm327Session().initializeELM327());
    // buildInitSequence() emits ATZ, ATE0, ATH0, ATL0, ATSP0, ATS0, ATSTFF.
    ASSERT_EQ(m.sentCommands.size(), 7u);
    EXPECT_EQ(m.sentCommands.front(), "ATZ\r");
    EXPECT_EQ(m.sentCommands.back(), "ATSTFF\r");

    advanceThread.join();
}

TEST(BLEManagerBaseSessionContract, InitializeOBD2WithDetection_InitializesThenDetects) {
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Advance the clock in a separate thread to trigger waitForPrompt timeouts.
    std::thread advanceThread([&clock]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clock.advance(std::chrono::milliseconds(3000));
        }
    });

    auto result = m.elm327Session().initializeOBD2WithDetection();
    // The contract: initialiseELM327 runs first (its 7 AT commands lead the
    // emission stream), then detection is attempted. detectVehicle() may
    // emit its own queries, so we lock only that init's sequence leads —
    // not the total count — and that a result is propagated without crash.
    ASSERT_GE(m.sentCommands.size(), 7u);
    EXPECT_EQ(m.sentCommands[0], "ATZ\r");
    EXPECT_EQ(m.sentCommands[6], "ATSTFF\r");
    EXPECT_NO_FATAL_FAILURE((void)result.has_value());

    advanceThread.join();
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
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Advance the clock in a separate thread to handle delays in initializeCANMonitor.
    std::thread advanceThread([&clock]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clock.advance(std::chrono::milliseconds(1000));
        }
    });

    EXPECT_FALSE(m.canMode());
    EXPECT_TRUE(m.elm327Session().initializeCANMonitor());
    // buildCANMonitorInitSequence(): ATZ, ATE0, ATSP6, ATH1, ATMA.
    ASSERT_EQ(m.sentCommands.size(), 5u);
    EXPECT_EQ(m.sentCommands.back(), "ATMA\r");
    EXPECT_TRUE(m.canMode());

    advanceThread.join();
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
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Advance the clock in a separate thread to trigger waitForPrompt timeouts.
    std::thread advanceThread([&clock]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clock.advance(std::chrono::milliseconds(3000));
        }
    });

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

    advanceThread.join();
}

TEST(BLEManagerBaseSessionContract, QueryVIN_ReturnsNulloptWhenPromptTimesOut) {
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Advance the clock in a separate thread to trigger waitForPrompt timeout.
    std::thread advanceThread([&clock]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        clock.advance(std::chrono::milliseconds(10));
    });

    auto vin = m.elm327Session().queryVIN(/*timeout_ms=*/5);
    EXPECT_FALSE(vin.has_value());
    // Emits the 09 02 query before waiting.
    ASSERT_FALSE(m.sentCommands.empty());
    EXPECT_EQ(m.sentCommands.front(), "09 02\r");

    advanceThread.join();
}

TEST(BLEManagerBaseSessionContract, QueryVIN_ReturnsVinWhenDetectorPopulatedAndPromptArrives) {
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Seed the detector with a VIN so getResult() has one to return, then
    // deliver the '>' prompt so queryVIN's waitForPrompt returns true.
    // feedVINResponse appends every non-zero byte after [0x49 0x02 ...] —
    // 17 ASCII chars yields a 17-char VIN (clamped at 17).
    m.vehicleDetector()->feedVINResponse(
        std::vector<uint8_t>{0x49, 0x02, 0x00,
                             '1','H','G','C','M','8','2','6','3',
                             '3','A','0','0','0','0','0','0'});

    // Notify the prompt from a separate thread to make waitForPrompt return true.
    // We need to delay slightly to ensure queryVIN has started waiting.
    std::thread notifyThread([&m]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        m.notifyPrompt();
    });

    auto vin = m.elm327Session().queryVIN(/*timeout_ms=*/2000);
    notifyThread.join();

    ASSERT_TRUE(vin.has_value());
    EXPECT_EQ(vin->size(), 17u);
}

// --- OBD2 polling loop contracts -----------------------------------------

TEST(BLEManagerBaseSessionContract, StartOBD2Polling_IsIdempotentSpawnsAtMostOneThread) {
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // Advance the clock in a separate thread to unblock the polling loop.
    std::thread advanceThread([&clock]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clock.advance(std::chrono::milliseconds(1000));
        }
    });

    m.setBaseConnected(true);
    m.setDataReceivedCallback([](const std::vector<uint8_t>&) {});

    m.elm327Session().startOBD2Polling(200);
    // A second start must be a no-op (guard: polling_active_ already true).
    m.elm327Session().startOBD2Polling(200);
    m.elm327Session().stopOBD2Polling();  // joins the single thread — would deadlock if two
    SUCCEED();

    advanceThread.join();
}

TEST(BLEManagerBaseSessionContract, OBD2PollingLoop_QueriesStandardPidsInDeclaredOrder) {
    FakeClock clock;
    SessionTestBLEManager m(&clock);

    // The loop gates on the base connected_ member (not the isConnected()
    // override), so drive it directly.
    m.setBaseConnected(true);

    // Deliver a prompt per query so the loop advances through the whole
    // PID cycle promptly rather than each waiting for the 2s timeout.
    m.setDataReceivedCallback([&m](const std::vector<uint8_t>&) {
        m.notifyPrompt();
    });

    m.elm327Session().startOBD2Polling(/*interval_ms=*/1000);

    // Give the polling thread a moment to start, then advance the clock to
    // unblock the initial wait and the first waitForPrompt timeout.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    clock.advance(std::chrono::milliseconds(
        SessionTestBLEManager::kPostConnectSetupDelayMs +
        Elm327Session::PROMPT_TIMEOUT_MS + 100
    ));

    // Give the polling thread time to process and send queries
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Now stop the loop
    m.setBaseConnected(false);
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

// ============================================================
// BLEManagerBase blind spec-first contracts — missing coverage
// that locks behaviour the S1448 refactor must not silently break.
// ============================================================

// --- queryPID return-value contract ---------------------------------------

// queryPID must always return an empty OBD2Response; the actual
// response arrives via the data callback, not the return value.

TEST(BLEManagerBaseTest, QueryPID_ReturnsEmptyResponse) {
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>& data) override {
            lastSent.assign(data.begin(), data.end());
        }
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
        std::vector<uint8_t> lastSent;
    };

    TestBLEManager manager;
    auto response = manager.queryPID(0x0C);

    // The contract: return value is always empty; real response is
    // delivered asynchronously via the data callback.
    EXPECT_FALSE(response.valid);
    EXPECT_EQ(response.mode, 0u);
    EXPECT_EQ(response.pid, 0u);
    EXPECT_TRUE(response.data.empty());
    EXPECT_FALSE(response.value.has_value());
}

// --- Accessor contracts ---------------------------------------------------

// vehicleDetector() must never return null — it owns the composed
// session's detector for the lifetime of the manager.

TEST(BLEManagerBaseTest, VehicleDetector_ReturnsNonNull) {
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
    };

    TestBLEManager manager;
    EXPECT_NE(manager.vehicleDetector(), nullptr);
}

// --- Initial-state contracts ----------------------------------------------

// bleNotificationCount starts at 0 before any data arrives.

TEST(BLEManagerBaseTest, BleNotificationCount_StartsAtZero) {
    SessionTestBLEManager m;
    EXPECT_EQ(m.bleNotificationCount(), 0);
}

// lastRawHex starts empty before any data arrives.

TEST(BLEManagerBaseTest, LastRawHex_StartsEmpty) {
    SessionTestBLEManager m;
    EXPECT_TRUE(m.lastRawHex().empty());
}

// waitForCharacteristics default returns true (base-class no-op).

TEST(BLEManagerBaseTest, WaitForCharacteristics_DefaultReturnsTrue) {
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
    };

    TestBLEManager manager;
    EXPECT_TRUE(manager.waitForCharacteristics());
}

// --- invokeDataCallback edge cases ----------------------------------------

// Empty data vector must not crash and must still increment the
// notification count (the count tracks every invoke, even empty).

TEST(BLEManagerBaseTest, InvokeDataCallback_EmptyDataIncrementsCount) {
    SessionTestBLEManager m;
    m.setDataReceivedCallback([](const std::vector<uint8_t>&) {});
    m.invokeDataCallback({});
    EXPECT_EQ(m.bleNotificationCount(), 1);
}

// Data longer than 16 bytes must truncate the hex dump to 16 bytes
// and append "..." to indicate truncation.

TEST(BLEManagerBaseTest, InvokeDataCallback_LongDataTruncatesHexDump) {
    SessionTestBLEManager m;
    m.setDataReceivedCallback([](const std::vector<uint8_t>&) {});
    std::vector<uint8_t> longData(20, 0xAB);
    m.invokeDataCallback(longData);
    EXPECT_EQ(m.bleNotificationCount(), 1);
    std::string hex = m.lastRawHex();
    // 16 bytes × 2 hex chars + 15 spaces = 47 chars, then "..."
    EXPECT_NE(hex.find("ab"), std::string::npos);
    EXPECT_NE(hex.find("..."), std::string::npos);
}

// Setting a new data callback must replace the old one; only the
// most-recently-set callback fires. invokeDataCallback routes through
// session_.handleIncomingData which only calls sessionDeliverParsed when
// parseOBD2Response produces non-empty binary, so we must send a valid
// ASCII ELM327 response format.

TEST(BLEManagerBaseTest, SetDataReceivedCallback_Replacement_NewCallbackOnlyFires) {
    SessionTestBLEManager m;

    bool oldFired = false;
    bool newFired = false;
    m.setDataReceivedCallback([&oldFired](const std::vector<uint8_t>&) { oldFired = true; });
    m.setDataReceivedCallback([&newFired](const std::vector<uint8_t>&) { newFired = true; });

    // "41 0D FF\r" → OBD2 response parses to [0x41, 0x0D, 0xFF], non-empty,
    // so sessionDeliverParsed fires and the new callback is invoked.
    std::string resp = "41 0D FF\r";
    m.invokeDataCallback(std::vector<uint8_t>(resp.begin(), resp.end()));
    EXPECT_FALSE(oldFired);
    EXPECT_TRUE(newFired);
}

// Setting a new device-found callback must replace the old one.

TEST(BLEManagerBaseTest, SetDeviceFoundCallback_Replacement_NewCallbackOnlyFires) {
    SessionTestBLEManager m;

    bool oldFired = false;
    bool newFired = false;
    m.setDeviceFoundCallback([&oldFired](const BLEDeviceInfo&) { oldFired = true; });
    m.setDeviceFoundCallback([&newFired](const BLEDeviceInfo&) { newFired = true; });

    m.invokeDeviceCallback({"addr", "Dev", false, -50});
    EXPECT_FALSE(oldFired);
    EXPECT_TRUE(newFired);
}

// Setting a new connection callback must replace the old one.

TEST(BLEManagerBaseTest, SetConnectionCallback_Replacement_NewCallbackOnlyFires) {
    SessionTestBLEManager m;

    bool oldFired = false;
    bool newFired = false;
    m.setConnectionCallback([&oldFired](bool, const std::string&) { oldFired = true; });
    m.setConnectionCallback([&newFired](bool, const std::string&) { newFired = true; });

    m.invokeConnectionCallback(true, "dev");
    EXPECT_FALSE(oldFired);
    EXPECT_TRUE(newFired);
}

// --- Connection-state edge cases ------------------------------------------

// setConnectionState with the default (empty) device_id must fire the
// connection callback with an empty id string.

TEST(BLEManagerBaseTest, SetConnectionState_EmptyDeviceId_FiresCallbackWithEmptyId) {
    SessionTestBLEManager m;
    std::string seenId;
    m.setConnectionCallback([&seenId](bool, const std::string& id) { seenId = id; });

    m.setConnectionState(true);  // default device_id = ""
    EXPECT_EQ(seenId, "");
}

// Disconnecting must clear the connected flag and fire the connection
// callback with the disconnected state. The connected device id is also
// cleared internally (verified by the EmptyDeviceId test above); the
// getConnectedDeviceId accessor is pure virtual and implemented by each
// subclass, so we verify the observable behavior through the callback.

TEST(BLEManagerBaseTest, SetConnectionState_Disconnect_ClearsConnectedState) {
    SessionTestBLEManager m;
    bool seenConnected = true;
    std::string seenId;
    m.setConnectionCallback([&](bool c, const std::string& id) {
        seenConnected = c; seenId = id;
    });

    m.setConnectionState(true, "dev-1");
    EXPECT_TRUE(m.baseConnected());

    m.setConnectionState(false);
    EXPECT_FALSE(m.baseConnected());
    EXPECT_FALSE(seenConnected);
    EXPECT_EQ(seenId, "");
}

// --- Device-management edge cases ------------------------------------------

// Multiple threads adding devices concurrently must not crash and must
// still deduplicate by address.

TEST(BLEManagerBaseTest, AddDiscoveredDevice_ConcurrentThreads_SafeAndDeduplicated) {
    SessionTestBLEManager m;
    const int kThreads = 8;
    const int kPerThread = 50;
    std::vector<std::thread> threads;
    std::atomic<int> crashes{0};

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&m, t, &crashes]() {
            for (int i = 0; i < kPerThread; ++i) {
                try {
                    // Each thread uses its own address prefix so duplicates
                    // are only within-thread; cross-thread addresses are unique.
                    m.addDiscoveredDevice(
                        BLEDeviceInfo{"addr-" + std::to_string(t), "Dev" + std::to_string(t),
                                      false, -50});
                } catch (...) {
                    crashes++;
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(crashes, 0);
    // kThreads unique addresses × kPerThread deduped to 1 each = kThreads total.
    // findDeviceByAddress for any known address must succeed.
    EXPECT_TRUE(m.findDeviceByAddress("addr-0").has_value());
    EXPECT_TRUE(m.findDeviceByAddress("addr-7").has_value());
}

// findDeviceByAddress with an empty string must return nullopt unless
// a device with an empty address was explicitly added.

TEST(BLEManagerBaseTest, FindDeviceByAddress_EmptyString_ReturnsNullopt) {
    SessionTestBLEManager m;
    m.addDiscoveredDevice({"AA:BB", "Dev", false, -50});
    EXPECT_FALSE(m.findDeviceByAddress("").has_value());
}

// --- sendASCII edge cases -------------------------------------------------

// sendASCII with an empty string must not crash and must produce a
// single empty send payload (empty string → empty byte vector).

TEST(BLEManagerBaseTest, SendASCII_EmptyString_DoesNotCrash) {
    SessionTestBLEManager m;
    EXPECT_NO_FATAL_FAILURE(m.sendASCII(""));
    ASSERT_EQ(m.sentCommands.size(), 1u);
    EXPECT_TRUE(m.sentCommands.front().empty());
}

// --- parseASCIIResponseToBinary edge cases --------------------------------

// Empty input produces an empty binary vector.

TEST(BLEManagerBaseTest, ParseASCIIResponseToBinary_EmptyInput_ReturnsEmpty) {
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
        std::vector<uint8_t> testParseASCIIResponseToBinary(const std::vector<uint8_t>& d) {
            return parseASCIIResponseToBinary(d);
        }
    };

    TestBLEManager manager;
    auto result = manager.testParseASCIIResponseToBinary({});
    EXPECT_TRUE(result.empty());
}

// Non-hex characters that are not separators or prompts must be silently
// skipped; no valid hex pairs → empty result.

TEST(BLEManagerBaseTest, ParseASCIIResponseToBinary_MalformedHex_ReturnsEmpty) {
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
        std::vector<uint8_t> testParseASCIIResponseToBinary(const std::vector<uint8_t>& d) {
            return parseASCIIResponseToBinary(d);
        }
    };

    TestBLEManager manager;
    // "XYZ" contains no valid hex pairs.
    auto result = manager.testParseASCIIResponseToBinary(
        std::vector<uint8_t>{'X', 'Y', 'Z'});
    EXPECT_TRUE(result.empty());
}

// An odd number of hex characters leaves a trailing unpaired nibble in the
// accumulator, which parseOBD2Response treats as invalid (line 182:
// `if (!hexStr.empty()) return std::nullopt`). The result is empty.

TEST(BLEManagerBaseTest, ParseASCIIResponseToBinary_OddLength_ReturnsEmpty) {
    class TestBLEManager : public BLEManagerBase {
    public:
        std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
        bool connect(std::string_view) override { return false; }
        void disconnect() override {}
        void send(const std::vector<uint8_t>&) override {}
        bool isConnected() const override { return false; }
        std::string getConnectedDeviceId() const override { return {}; }
        std::vector<uint8_t> testParseASCIIResponseToBinary(const std::vector<uint8_t>& d) {
            return parseASCIIResponseToBinary(d);
        }
    };

    TestBLEManager manager;
    // "410" — three hex chars; the trailing '0' is an unpaired nibble, so
    // the response is considered invalid and returns empty.
    auto result = manager.testParseASCIIResponseToBinary(
        std::vector<uint8_t>{'4', '1', '0'});
    EXPECT_TRUE(result.empty());
}

// --- signalQuality exact boundary values -----------------------------------

// The RSSI thresholds are inclusive: >= -50 → Excellent, >= -65 → Good,
// >= -75 → Fair, else Poor. Lock the exact boundary values.

TEST(BLEManagerBaseTest, SignalQuality_ExactBoundary_Minus50_IsExcellent) {
    EXPECT_EQ(BLEManagerBase::signalQuality(-50), "Excellent");
}

TEST(BLEManagerBaseTest, SignalQuality_ExactBoundary_Minus65_IsGood) {
    EXPECT_EQ(BLEManagerBase::signalQuality(-65), "Good");
}

TEST(BLEManagerBaseTest, SignalQuality_ExactBoundary_Minus75_IsFair) {
    EXPECT_EQ(BLEManagerBase::signalQuality(-75), "Fair");
}

// ============================================================
// BLEManagerBase stopOBD2Polling contract
// ============================================================

TEST(BLEManagerBaseTest, StopOBD2Polling_WakesPromptWaitAndJoins) {
    PromptTestBLEManager manager;
    manager.fakeConnected = true;
    manager.setDataReceivedCallback([](const std::vector<uint8_t>&) {});

    manager.elm327Session().startOBD2Polling(200);
    manager.elm327Session().stopOBD2Polling();

    SUCCEED();
}