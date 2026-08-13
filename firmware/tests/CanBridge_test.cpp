// CanBridge_test.cpp - Tests for CanBridge vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/CanBridge.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::SaveArg;

// Mock TCP Client interface
class MockTcpClient : public ITcpClient {
public:
    MOCK_METHOD(bool, connected, (), (const, override));
    MOCK_METHOD(size_t, print, (const char* str), (override));
    MOCK_METHOD(void, flush, (), (override));

    std::string printedData;

    void delegateToDummy() {
        ON_CALL(*this, print(_)).WillByDefault([this](const char* str) {
            printedData += str;
            return strlen(str);
        });
        ON_CALL(*this, flush()).WillByDefault([]() {});
    }

    void reset() {
        printedData.clear();
    }
};

// Mock Serial interface
class MockSerialCan : public ISerialCan {
public:
    MOCK_METHOD(size_t, print, (const char* str), (override));
    MOCK_METHOD(void, flush, (), (override));

    std::string printedData;

    void delegateToDummy() {
        ON_CALL(*this, print(_)).WillByDefault([this](const char* str) {
            printedData += str;
            return strlen(str);
        });
        ON_CALL(*this, flush()).WillByDefault([]() {});
    }

    void reset() {
        printedData.clear();
    }
};

// Mock CAN Driver interface
class MockCanDriver : public ICanDriver {
public:
    MOCK_METHOD(int, driverInstall, (CanGeneralConfig*, CanTimingConfig*, CanFilterConfig*), (override));
    MOCK_METHOD(int, start, (), (override));
    MOCK_METHOD(int, receive, (CanFrame* msg, uint32_t timeoutMs), (override));

    std::queue<CanFrame> frameQueue;

    void delegateToDummy() {
        ON_CALL(*this, driverInstall(_, _, _)).WillByDefault(Return(0));
        ON_CALL(*this, start()).WillByDefault(Return(0));
        ON_CALL(*this, receive(_, _)).WillByDefault([this](CanFrame* msg, uint32_t) {
            if (frameQueue.empty()) {
                return -1;  // ESP_ERR_TIMEOUT or similar
            }
            *msg = frameQueue.front();
            frameQueue.pop();
            return 0;  // ESP_OK
        });
    }

    void pushFrame(const CanFrame& frame) {
        frameQueue.push(frame);
    }

    void reset() {
        while (!frameQueue.empty()) frameQueue.pop();
    }
};

class CanBridgeTest : public ::testing::Test {
protected:
    MockCanDriver canDriverMock;
    MockTcpClient tcpClientMock;
    MockSerialCan serialMock;
    std::unique_ptr<CanBridge> canBridge;

    void SetUp() override {
        canDriverMock.reset();
        tcpClientMock.reset();
        serialMock.reset();

        canDriverMock.delegateToDummy();
        tcpClientMock.delegateToDummy();
        serialMock.delegateToDummy();

        canBridge = std::make_unique<CanBridge>(
            canDriverMock, tcpClientMock, serialMock
        );
    }

    void TearDown() override {
        canBridge.reset();
    }
};

// Pure function tests
TEST_F(CanBridgeTest, BuildFrameString_SingleByteFrame) {
    CanFrame msg;
    msg.identifier = 0x123;
    msg.data_length_code = 1;
    msg.data[0] = 0xAB;

    char buf[64];
    CanBridge::buildFrameString(msg, buf, sizeof(buf));

    EXPECT_STREQ(buf, "123 AB\r");
}

TEST_F(CanBridgeTest, BuildFrameString_ExtendedId) {
    CanFrame msg;
    msg.identifier = 0x1ABCDEF;
    msg.data_length_code = 0;

    char buf[64];
    CanBridge::buildFrameString(msg, buf, sizeof(buf));

    // Prints the full identifier as hex (no truncation)
    EXPECT_STREQ(buf, "1ABCDEF\r");
}

TEST_F(CanBridgeTest, BuildFrameString_MaxDataLength) {
    CanFrame msg;
    msg.identifier = 0x7DF;
    msg.data_length_code = 8;
    msg.data[0] = 0x01; msg.data[1] = 0x02; msg.data[2] = 0x03; msg.data[3] = 0x04;
    msg.data[4] = 0x05; msg.data[5] = 0x06; msg.data[6] = 0x07; msg.data[7] = 0x08;

    char buf[64];
    CanBridge::buildFrameString(msg, buf, sizeof(buf));

    EXPECT_STREQ(buf, "7DF 01 02 03 04 05 06 07 08\r");
}

TEST_F(CanBridgeTest, BuildFrameString_ClampsDataLength) {
    CanFrame msg;
    msg.identifier = 0x100;
    msg.data_length_code = 10;  // Exceeds MAX_DATA_LENGTH (8)
    msg.data[0] = 0xAA; msg.data[1] = 0xBB; msg.data[2] = 0xCC; msg.data[3] = 0xDD;
    msg.data[4] = 0xEE; msg.data[5] = 0xFF; msg.data[6] = 0x11; msg.data[7] = 0x22;

    char buf[64];
    CanBridge::buildFrameString(msg, buf, sizeof(buf));

    // Should clamp to 8 bytes
    EXPECT_STREQ(buf, "100 AA BB CC DD EE FF 11 22\r");
}

TEST_F(CanBridgeTest, BuildFrameString_ZeroLength) {
    CanFrame msg;
    msg.identifier = 0x200;
    msg.data_length_code = 0;

    char buf[64];
    CanBridge::buildFrameString(msg, buf, sizeof(buf));

    EXPECT_STREQ(buf, "200\r");
}

// Initialization tests
TEST_F(CanBridgeTest, Init_ReturnsTrue) {
    EXPECT_TRUE(canBridge->init());  // Should return true
    EXPECT_FALSE(canBridge->isMonitorActive());  // init() doesn't set monitorActive
}

TEST_F(CanBridgeTest, ProcessFrames_BeforeInit_DoesNotProcess) {
    CanFrame msg;
    msg.identifier = 0x123;
    msg.data_length_code = 1;
    msg.data[0] = 0xAB;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(false, /*nowMs=*/1000, 0);

    EXPECT_TRUE(serialMock.printedData.empty());
}

TEST_F(CanBridgeTest, ProcessFrames_AfterInit_ProcessesFrames) {
    canBridge->init();

    CanFrame msg;
    msg.identifier = 0x123;
    msg.data_length_code = 1;
    msg.data[0] = 0xAB;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(false, /*nowMs=*/1000, 0);

    EXPECT_FALSE(serialMock.printedData.empty());
    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("123"));
    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("AB"));
}

// Serial output tests
TEST_F(CanBridgeTest, ProcessFrames_NoQuietPeriod_WritesToSerial) {
    canBridge->init();

    CanFrame msg;
    msg.identifier = 0x456;
    msg.data_length_code = 2;
    msg.data[0] = 0xCD; msg.data[1] = 0xEF;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(false, /*nowMs=*/1000, 0);  // No quiet period

    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("456 CD EF"));
}

TEST_F(CanBridgeTest, ProcessFrames_DuringQuietPeriod_SuppressesSerial) {
    canBridge->init();

    CanFrame msg;
    msg.identifier = 0x789;
    msg.data_length_code = 1;
    msg.data[0] = 0xAA;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(false, /*nowMs=*/1000, 10000);  // Quiet period active

    EXPECT_TRUE(serialMock.printedData.empty());
}

// ── Serial quiet-window is a DEADLINE, not a latch ──────────────────────────
// BUSINESS SCENARIO: an AT command arrives on the USB serial link. The firmware
// mutes CAN frame echo for a short settling window (millis() + 250) so the
// command's reply is not interleaved with frame spam. Once that instant passes,
// serial CAN logging MUST resume — the field engineer's USB console is the only
// diagnostic channel when WiFi is down, and a permanently muted console looks
// identical to a dead CAN bus.
//
// The regression these pin: reading serialQuietUntilMs as a boolean (`> 0`)
// makes the very first AT command mute serial forever, because the deadline is
// a non-zero timestamp that is never cleared back to 0.

TEST_F(CanBridgeTest, ProcessFrames_AfterQuietDeadlinePasses_ResumesSerial) {
    canBridge->init();

    CanFrame msg;
    msg.identifier = 0x7E8;
    msg.data_length_code = 1;
    msg.data[0] = 0x41;
    canDriverMock.pushFrame(msg);

    // Deadline was armed at t=1000 (millis()+250); we are now at t=1300, past it.
    canBridge->processFrames(false, /*nowMs=*/1300, /*serialQuietUntilMs=*/1250);

    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("7E8 41"))
        << "serial frame echo must resume once the quiet deadline has elapsed";
}

TEST_F(CanBridgeTest, ProcessFrames_AtQuietDeadlineInstant_ResumesSerial) {
    canBridge->init();

    CanFrame msg;
    msg.identifier = 0x7E9;
    msg.data_length_code = 1;
    msg.data[0] = 0x42;
    canDriverMock.pushFrame(msg);

    // Boundary: nowMs == deadline. The window is [armed, deadline) — the
    // deadline instant itself is no longer quiet.
    canBridge->processFrames(false, /*nowMs=*/1250, /*serialQuietUntilMs=*/1250);

    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("7E9 42"))
        << "the quiet window is half-open: at the deadline instant serial is live again";
}

TEST_F(CanBridgeTest, ProcessFrames_BeforeQuietDeadline_StillSuppressesSerial) {
    canBridge->init();

    CanFrame msg;
    msg.identifier = 0x7EA;
    msg.data_length_code = 1;
    msg.data[0] = 0x43;
    canDriverMock.pushFrame(msg);

    // Inside the window: armed at 1000, deadline 1250, now 1100.
    canBridge->processFrames(false, /*nowMs=*/1100, /*serialQuietUntilMs=*/1250);

    EXPECT_TRUE(serialMock.printedData.empty())
        << "serial echo stays muted while the quiet window is still open";
}

TEST_F(CanBridgeTest, ProcessFrames_QuietWindowDoesNotGateTcpStream) {
    // The quiet window exists to keep the USB console readable during an AT
    // exchange. It must NOT stall the TCP CAN stream the host CLI depends on.
    canBridge->init();
    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(true));

    CanFrame msg;
    msg.identifier = 0x7EB;
    msg.data_length_code = 1;
    msg.data[0] = 0x44;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(/*monitorActive=*/true, /*nowMs=*/1100,
                             /*serialQuietUntilMs=*/1250);

    EXPECT_TRUE(serialMock.printedData.empty());
    EXPECT_THAT(tcpClientMock.printedData, testing::HasSubstr("7EB 44"))
        << "TCP streaming is independent of the serial quiet window";
}

// TCP output tests
TEST_F(CanBridgeTest, ProcessFrames_NoTcpClient_NoTcpOutput) {
    canBridge->init();

    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(false));

    CanFrame msg;
    msg.identifier = 0x111;
    msg.data_length_code = 1;
    msg.data[0] = 0x22;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(true, /*nowMs=*/1000, 0);  // Monitor active

    EXPECT_TRUE(tcpClientMock.printedData.empty());
}

TEST_F(CanBridgeTest, ProcessFrames_TcpConnectedButMonitorInactive_NoTcpOutput) {
    canBridge->init();

    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(true));

    CanFrame msg;
    msg.identifier = 0x222;
    msg.data_length_code = 1;
    msg.data[0] = 0x33;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(false, /*nowMs=*/1000, 0);  // Monitor NOT active

    EXPECT_TRUE(tcpClientMock.printedData.empty());
}

TEST_F(CanBridgeTest, ProcessFrames_TcpConnectedAndMonitorActive_WritesToTcp) {
    canBridge->init();

    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(true));

    CanFrame msg;
    msg.identifier = 0x333;
    msg.data_length_code = 1;
    msg.data[0] = 0x44;
    canDriverMock.pushFrame(msg);

    canBridge->processFrames(true, /*nowMs=*/1000, 0);  // Monitor active

    EXPECT_THAT(tcpClientMock.printedData, testing::HasSubstr("333"));
    EXPECT_THAT(tcpClientMock.printedData, testing::HasSubstr("44"));
}

// Multiple frames tests
TEST_F(CanBridgeTest, ProcessFrames_MultipleFrames_ProcessesAll) {
    canBridge->init();

    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(true));

    CanFrame msg1, msg2, msg3;
    msg1.identifier = 0x101; msg1.data_length_code = 1; msg1.data[0] = 0x01;
    msg2.identifier = 0x202; msg2.data_length_code = 2; msg2.data[0] = 0x02; msg2.data[1] = 0x03;
    msg3.identifier = 0x303; msg3.data_length_code = 1; msg3.data[0] = 0x04;

    canDriverMock.pushFrame(msg1);
    canDriverMock.pushFrame(msg2);
    canDriverMock.pushFrame(msg3);

    canBridge->processFrames(true, /*nowMs=*/1000, 0);

    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("101 01"));
    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("202 02 03"));
    EXPECT_THAT(serialMock.printedData, testing::HasSubstr("303 04"));

    EXPECT_THAT(tcpClientMock.printedData, testing::HasSubstr("101 01"));
    EXPECT_THAT(tcpClientMock.printedData, testing::HasSubstr("202 02 03"));
    EXPECT_THAT(tcpClientMock.printedData, testing::HasSubstr("303 04"));
}

// Monitor state tests
TEST_F(CanBridgeTest, SetMonitorActive_UpdatesState) {
    canBridge->setMonitorActive(true);
    EXPECT_TRUE(canBridge->isMonitorActive());

    canBridge->setMonitorActive(false);
    EXPECT_FALSE(canBridge->isMonitorActive());
}

// Phase 1 latency: each CAN frame written to the TCP client must be flushed
// immediately. Without an explicit flush the ESP32 WiFiClient buffers the bytes
// and Nagle + the LwIP send buffer can hold them up to the delayed-ACK window —
// the dominant source of variable >100 ms stalls on the link.
TEST_F(CanBridgeTest, ProcessFrames_TcpStream_FlushesEachFrame) {
    canBridge->init();
    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(true));

    // Count flush() calls on the TCP path precisely (no delegateToDummy no-op).
    int flushCalls = 0;
    EXPECT_CALL(tcpClientMock, flush()).WillRepeatedly([&flushCalls]() { ++flushCalls; });

    CanFrame msg1, msg2;
    msg1.identifier = 0x401; msg1.data_length_code = 1; msg1.data[0] = 0x55;
    msg2.identifier = 0x402; msg2.data_length_code = 1; msg2.data[0] = 0x66;
    canDriverMock.pushFrame(msg1);
    canDriverMock.pushFrame(msg2);

    canBridge->processFrames(/*monitorActive=*/true, /*nowMs=*/1000, 0);

    // Two frames streamed -> two explicit flushes (one per frame).
    EXPECT_EQ(flushCalls, 2)
        << "every TCP-streamed frame must be flushed immediately, not batched";
}

// Phase 1 latency: flush must NOT be skipped when a frame is streamed. Regression
// guard asserting the per-frame flush is not conditional on serial quiet state.
TEST_F(CanBridgeTest, ProcessFrames_TcpStream_FlushesEvenDuringSerialQuiet) {
    canBridge->init();
    ON_CALL(tcpClientMock, connected()).WillByDefault(Return(true));

    int flushCalls = 0;
    EXPECT_CALL(tcpClientMock, flush()).WillRepeatedly([&flushCalls]() { ++flushCalls; });

    CanFrame msg;
    msg.identifier = 0x403; msg.data_length_code = 1; msg.data[0] = 0x77;
    canDriverMock.pushFrame(msg);

    // Serial is in its quiet window — TCP must still flush.
    canBridge->processFrames(/*monitorActive=*/true, /*nowMs=*/1100,
                             /*serialQuietUntilMs=*/1250);

    EXPECT_EQ(flushCalls, 1)
        << "TCP flush must be independent of the serial quiet window";
}

TEST_F(CanBridgeTest, ProcessFrames_UpdatesMonitorState) {
    canBridge->init();
    EXPECT_FALSE(canBridge->isMonitorActive());

    canBridge->processFrames(true, /*nowMs=*/1000, 0);
    EXPECT_TRUE(canBridge->isMonitorActive());

    canBridge->processFrames(false, /*nowMs=*/1000, 0);
    EXPECT_FALSE(canBridge->isMonitorActive());
}
