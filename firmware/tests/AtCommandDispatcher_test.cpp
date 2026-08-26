// AtCommandDispatcher_test.cpp - Tests for AtCommandDispatcher vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/AtCommandDispatcher.h"
#include "mocks/ArduinoMock.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;

// Mock TCP Client interface
class MockTcpClientAt : public ITcpClientAt {
public:
    MOCK_METHOD(void, print, (const char* str), (override));
    MOCK_METHOD(void, flush, (), (override));

    void delegateToDummy() {
        ON_CALL(*this, print(_)).WillByDefault([](const char*) {});
        ON_CALL(*this, flush()).WillByDefault([]() {});
    }

    void reset() {}
};

// Mock Serial interface
class MockSerialAt : public ISerialAt {
public:
    MOCK_METHOD(void, println, (const char* str), (override));
    MOCK_METHOD(void, flush, (), (override));

    std::string lastPrint;

    void delegateToDummy() {
        ON_CALL(*this, println(_)).WillByDefault([this](const char* str) {
            lastPrint = str ? str : "";
        });
        ON_CALL(*this, flush()).WillByDefault([]() {});
    }

    void reset() {
        lastPrint.clear();
    }
};

// Mock ESP interface
class MockEspAt : public IEspAt {
public:
    MOCK_METHOD(void, restart, (), (override));

    bool restarted{false};

    void delegateToDummy() {
        ON_CALL(*this, restart()).WillByDefault([this]() {
            restarted = true;
        });
    }

    void reset() {
        restarted = false;
    }
};

// Mock WiFi credential store interface
class MockWifiCredentialStore : public IWifiCredentialStore {
public:
    MOCK_METHOD(bool, store, (const std::string& ssid, const std::string& password), (override));
    MOCK_METHOD(bool, load, (std::string & ssid, std::string & pass), (override));

    void delegateToDummy() {
        ON_CALL(*this, store(_, _)).WillByDefault([](const std::string&, const std::string&) {
            return true;
        });
        ON_CALL(*this, load(_, _)).WillByDefault([](std::string&, std::string&) {
            return false;
        });
    }

    void reset() {}
};

// Mock monitor state interface
class MockMonitorState : public IMonitorState {
public:
    MOCK_METHOD(void, setMonitorActive, (bool active), (override));

    void delegateToDummy() {
        ON_CALL(*this, setMonitorActive(_)).WillByDefault([](bool) {});
    }

    void reset() {}
};

// Mock WiFi token store interface
class MockTokenStore : public IWifiTokenStore {
public:
    MOCK_METHOD(bool, storeToken, (const std::string& token), (override));

    void delegateToDummy() {
        ON_CALL(*this, storeToken(_)).WillByDefault([](const std::string&) {
            return true;
        });
    }

    void reset() {}
};

// Mock WiFi credential clear interface
class MockCredentialClear : public IWifiCredentialClear {
public:
    MOCK_METHOD(bool, clear, (), (override));

    void delegateToDummy() {
        ON_CALL(*this, clear()).WillByDefault([]() { return true; });
    }

    void reset() {}
};

// Test handler that matches a specific command
class TestCommandHandler : public IAtCommandHandler {
public:
    std::string matchCmd;
    AtCommandResult result;

    TestCommandHandler(const std::string& cmd, const char* resp = "OK")
        : matchCmd(cmd), result(resp) {}

    bool matches(const std::string& normalizedCmd) const override {
        return normalizedCmd == matchCmd;
    }

    AtCommandResult execute(const std::string& /* originalCmd */) const override {
        return result;
    }
};

class AtCommandDispatcherTest : public ::testing::Test {
protected:
    MockTcpClientAt tcpClientMock;
    MockSerialAt serialMock;
    MockEspAt espMock;
    MockWifiCredentialStore wifiStoreMock;
    MockTokenStore tokenStoreMock;
    MockCredentialClear credClearMock;
    MockMonitorState monitorMock;
    std::array<uint8_t, 16> deviceIdMock = {};
    std::unique_ptr<AtCommandDispatcher> dispatcher;

    void SetUp() override {
        tcpClientMock.reset();
        serialMock.reset();
        espMock.reset();
        wifiStoreMock.reset();
        tokenStoreMock.reset();
        credClearMock.reset();
        monitorMock.reset();
        arduino_mock::resetAllMocks();

        tcpClientMock.delegateToDummy();
        serialMock.delegateToDummy();
        espMock.delegateToDummy();
        wifiStoreMock.delegateToDummy();
        tokenStoreMock.delegateToDummy();
        credClearMock.delegateToDummy();
        monitorMock.delegateToDummy();

        dispatcher = std::make_unique<AtCommandDispatcher>(
            tcpClientMock, serialMock, espMock, wifiStoreMock,
            tokenStoreMock, credClearMock, monitorMock, deviceIdMock
        );
    }

    void TearDown() override {
        dispatcher.reset();
    }
};

// Pure function tests
TEST_F(AtCommandDispatcherTest, NormalizeAtCommand_TrimsWhitespace) {
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("  AT  "), "AT");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("\tAT\t"), "AT");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("\r\nAT\r\n"), "AT");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("  \r\n\tAT\t\r\n  "), "AT");
}

TEST_F(AtCommandDispatcherTest, NormalizeAtCommand_ConvertsToUppercase) {
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("at"), "AT");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("at+gmi"), "AT+GMI");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("At+HeLo"), "AT+HELO");
}

TEST_F(AtCommandDispatcherTest, NormalizeAtCommand_EmptyString) {
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand(""), "");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("   "), "");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("\t\r\n"), "");
}

TEST_F(AtCommandDispatcherTest, NormalizeAtCommand_PreservesContent) {
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("AT+HELO"), "AT+HELO");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("AT DEVICEID"), "AT DEVICEID");
}

TEST_F(AtCommandDispatcherTest, BuildHeloResponse_CorrectFormat) {
    std::array<uint8_t, 16> deviceId = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
    };

    std::string response = AtCommandDispatcher::buildHeloResponse(deviceId, "ESP32-CAN-Bridge", "0.2.0");

    EXPECT_THAT(response, testing::HasSubstr("ACK DEVICE=ESP32-CAN-Bridge"));
    EXPECT_THAT(response, testing::HasSubstr("FIRMWARE=0.2.0"));
    EXPECT_THAT(response, testing::HasSubstr("DEVICEID="));
    EXPECT_THAT(response, testing::HasSubstr("01234567"));
    EXPECT_THAT(response, testing::HasSubstr("89ABCDEF"));
}

TEST_F(AtCommandDispatcherTest, ParseSetWifiParams_ValidFormat) {
    SetWifiParams params = AtCommandDispatcher::parseSetWifiParams("MySSID,MyPassword");

    EXPECT_TRUE(params.valid);
    EXPECT_EQ(params.ssid, "MySSID");
    EXPECT_EQ(params.password, "MyPassword");
}

TEST_F(AtCommandDispatcherTest, ParseSetWifiParams_WithCommaInPassword) {
    SetWifiParams params = AtCommandDispatcher::parseSetWifiParams("MySSID,Pass,word");

    EXPECT_TRUE(params.valid);
    EXPECT_EQ(params.ssid, "MySSID");
    EXPECT_EQ(params.password, "Pass,word");  // Everything after first comma
}

TEST_F(AtCommandDispatcherTest, ParseSetWifiParams_NoComma) {
    SetWifiParams params = AtCommandDispatcher::parseSetWifiParams("MySSID");

    EXPECT_FALSE(params.valid);
}

TEST_F(AtCommandDispatcherTest, ParseSetWifiParams_EmptySSID) {
    SetWifiParams params = AtCommandDispatcher::parseSetWifiParams(",MyPassword");

    EXPECT_FALSE(params.valid);
}

TEST_F(AtCommandDispatcherTest, ParseSetWifiParams_EmptyPassword) {
    SetWifiParams params = AtCommandDispatcher::parseSetWifiParams("MySSID,");

    EXPECT_TRUE(params.valid);
    EXPECT_EQ(params.ssid, "MySSID");
    EXPECT_EQ(params.password, "");
}

TEST_F(AtCommandDispatcherTest, IsValidAuthToken_MatchingToken) {
    EXPECT_TRUE(AtCommandDispatcher::isValidAuthToken("AUTH ABC123", "ABC123"));
    EXPECT_TRUE(AtCommandDispatcher::isValidAuthToken("AUTH XYZZY", "XYZZY"));
}

TEST_F(AtCommandDispatcherTest, IsValidAuthToken_NonMatchingToken) {
    EXPECT_FALSE(AtCommandDispatcher::isValidAuthToken("AUTH ABC123", "WRONG"));
    EXPECT_FALSE(AtCommandDispatcher::isValidAuthToken("AUTH ABC123", "ABC1234"));
}

TEST_F(AtCommandDispatcherTest, IsValidAuthToken_DifferentCommand) {
    EXPECT_FALSE(AtCommandDispatcher::isValidAuthToken("AT+HELO", "ABC123"));
    EXPECT_FALSE(AtCommandDispatcher::isValidAuthToken("ABC123", "ABC123"));
}

// Command handling tests
TEST_F(AtCommandDispatcherTest, HandleSerialCommand_PrintsToSerial) {
    auto handler = std::make_unique<TestCommandHandler>("AT+PING", "PONG");
    dispatcher->registerHandler(std::move(handler));

    EXPECT_CALL(serialMock, println(::testing::StrEq("PONG")));
    dispatcher->handleSerialCommand("AT+PING");
}

// ATDUMPWIFI surfaces the stored-credential state the boot reader sees.
TEST_F(AtCommandDispatcherTest, Atdumpwifi_NoStoredCredentials_ReportsNone) {
    ON_CALL(wifiStoreMock, load(_, _)).WillByDefault([](std::string& ssid, std::string& pass) {
        ssid.clear();
        pass.clear();
        return false;
    });

    EXPECT_CALL(serialMock, println(::testing::StrEq("OK no stored credentials")));
    dispatcher->handleSerialCommand("ATDUMPWIFI");
}

TEST_F(AtCommandDispatcherTest, Atdumpwifi_StoredCredentials_ReportsSsidAndLen) {
    ON_CALL(wifiStoreMock, load(_, _)).WillByDefault([](std::string& ssid, std::string& pass) {
        ssid = "manht2";
        pass = "luckyshoe478";
        return true;
    });

    EXPECT_CALL(serialMock,
                println(::testing::StrEq("OK stored ssid=manht2 pass_len=12")));
    dispatcher->handleSerialCommand("ATDUMPWIFI");
}

TEST_F(AtCommandDispatcherTest, HandleSerialCommand_NoMatch_PrintsQuestionMark) {
    EXPECT_CALL(serialMock, println(::testing::StrEq("?")));
    dispatcher->handleSerialCommand("AT+UNKNOWN");
}

TEST_F(AtCommandDispatcherTest, HandleSerialCommand_ShouldFlush_FlushesTcp) {
    auto handler = std::make_unique<TestCommandHandler>("AT+FLUSH");
    handler->result = AtCommandResult("OK", false, true);  // shouldFlush = true
    dispatcher->registerHandler(std::move(handler));

    EXPECT_CALL(serialMock, println(::testing::StrEq("OK")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(serialMock, println(::testing::StrEq("REBOOT")));
    EXPECT_CALL(serialMock, flush());
    dispatcher->handleSerialCommand("AT+FLUSH");
}

TEST_F(AtCommandDispatcherTest, HandleSerialCommand_ShouldReboot_RestartsEsp) {
    auto handler = std::make_unique<TestCommandHandler>("AT+REBOOT");
    handler->result = AtCommandResult("OK", true, false);  // shouldReboot = true
    dispatcher->registerHandler(std::move(handler));

    EXPECT_CALL(serialMock, println(::testing::StrEq("OK")));
    EXPECT_CALL(espMock, restart());
    dispatcher->handleSerialCommand("AT+REBOOT");
}

// Multiple handlers tests
TEST_F(AtCommandDispatcherTest, MultipleHandlers_MatchesFirst) {
    auto handler1 = std::make_unique<TestCommandHandler>("AT+TEST", "HANDLER1");
    auto handler2 = std::make_unique<TestCommandHandler>("AT+TEST", "HANDLER2");

    dispatcher->registerHandler(std::move(handler1));
    dispatcher->registerHandler(std::move(handler2));

    EXPECT_CALL(serialMock, println(::testing::StrEq("HANDLER1")));
    dispatcher->handleSerialCommand("AT+TEST");
}

TEST_F(AtCommandDispatcherTest, MultipleHandlers_DifferentCommands) {
    auto handler1 = std::make_unique<TestCommandHandler>("AT+CMD1", "RESP1");
    auto handler2 = std::make_unique<TestCommandHandler>("AT+CMD2", "RESP2");

    dispatcher->registerHandler(std::move(handler1));
    dispatcher->registerHandler(std::move(handler2));

    EXPECT_CALL(serialMock, println(::testing::StrEq("RESP1")));
    dispatcher->handleSerialCommand("AT+CMD1");

    EXPECT_CALL(serialMock, println(::testing::StrEq("RESP2")));
    dispatcher->handleSerialCommand("AT+CMD2");
}

// Edge cases
TEST_F(AtCommandDispatcherTest, HandleEmptyCommand) {
    EXPECT_CALL(serialMock, println("?"));
    dispatcher->handleSerialCommand("");
}

TEST_F(AtCommandDispatcherTest, HandleWhitespaceOnlyCommand) {
    EXPECT_CALL(serialMock, println("?"));
    dispatcher->handleSerialCommand("   \t\r\n");
}

// ── TCP command path (handleTcpCommand) — previously UNTESTED ──────────────
// The contract: a TCP-driven AT command must be echoed to the TCP client (via
// sendTcpPrompt -> tcpClient_.print) framed with the ELM327 "\r\r>" terminator,
// NOT to the serial console. This is the exact path the macOS client exercises
// during its HELO handshake (ATI then ATHELO), so it must round-trip on the
// wire. These tests lock that contract.

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_Ati_PrintsToTcpClient) {
    // ATI must reach the TCP client framed as "<banner>\r\r>", never serial.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("ESP32 CAN Bridge v0.1\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(serialMock, println(::testing::_)).Times(0);  // not serial

    dispatcher->handleTcpCommand("ATI");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_Unknown_PrintsQuestionToTcp) {
    // An unmatched TCP command still gets a "?" framed response on the TCP link.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("?\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(serialMock, println(::testing::_)).Times(0);

    dispatcher->handleTcpCommand("ATNOPE");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_Ati_UnknownDoesNotCrossToSerial) {
    // Belt-and-suspenders: a TCP ATI must NOT also appear on the serial line.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("ESP32 CAN Bridge v0.1\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(serialMock, println(::testing::_)).Times(0);

    dispatcher->handleTcpCommand("ATI");
}

// ── Concrete firmware handler dispatch tests (extracted from .h to .cpp) ──────
// These lock the business behaviour of each AT command handler through the
// dispatcher's public TCP path. The dispatcher registers the canonical handler
// set lazily on first handle* call, so we just call handleTcpCommand directly.

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetWifi_ValidParams_StoresAndReboots) {
    // ATSETWIFI with a valid SSID/password pair should persist credentials and
    // signal reboot+flush so the TCP client sees the response before ESP.restart().
    EXPECT_CALL(wifiStoreMock, store("MySSID", "MyPass")).WillOnce(Return(true));
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("OK WiFi credentials stored")));
    EXPECT_CALL(tcpClientMock, flush()).Times(2);  // sendTcpPrompt + shouldFlushClient
    EXPECT_CALL(serialMock, println(::testing::StrEq("REBOOT")));
    EXPECT_CALL(serialMock, flush());
    EXPECT_CALL(espMock, restart());

    dispatcher->handleTcpCommand("ATSETWIFI MySSID,MyPass");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetWifi_InvalidFormat_ReturnsError) {
    // Missing comma means parseSetWifiParams returns invalid — no store call.
    EXPECT_CALL(wifiStoreMock, store(_, _)).Times(0);
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Invalid format")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETWIFI MySSID");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetWifi_EmptySsid_ReturnsError) {
    // SSID must be 1-32 chars; empty SSID is rejected before store.
    // ATSETWIFI with whitespace then comma parses as invalid format (comma at
    // position 0 after trim), so the SSID-length check is unreachable from the
    // handler — parseSetWifiParams catches it first. This test asserts the
    // actual error the handler emits for that input.
    EXPECT_CALL(wifiStoreMock, store(_, _)).Times(0);
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Invalid format")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETWIFI  ,MyPass");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetWifi_SsidTooLong_ReturnsError) {
    // SSID length > 32 chars is rejected.
    std::string longSsid(33, 'A');
    EXPECT_CALL(wifiStoreMock, store(_, _)).Times(0);
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Invalid SSID length")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETWIFI " + longSsid + ",MyPass");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetWifi_PasswordTooLong_ReturnsError) {
    // Password length > 64 chars is rejected.
    std::string longPass(65, 'B');
    EXPECT_CALL(wifiStoreMock, store(_, _)).Times(0);
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Invalid password length")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETWIFI MySSID," + longPass);
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetWifi_StoreFailure_ReturnsError) {
    // NVS store returning false propagates as "ERROR Failed to store credentials".
    EXPECT_CALL(wifiStoreMock, store("MySSID", "MyPass")).WillOnce(Return(false));
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Failed to store credentials")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETWIFI MySSID,MyPass");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetToken_ValidToken_StoresAndReboots) {
    // ATSETTOKEN stores the auth token and signals reboot.
    EXPECT_CALL(tokenStoreMock, storeToken("MySecretToken")).WillOnce(Return(true));
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("OK Token stored")));
    EXPECT_CALL(tcpClientMock, flush()).Times(2);  // sendTcpPrompt + shouldFlushClient
    EXPECT_CALL(serialMock, println(::testing::StrEq("REBOOT")));
    EXPECT_CALL(serialMock, flush());
    EXPECT_CALL(espMock, restart());

    dispatcher->handleTcpCommand("ATSETTOKEN MySecretToken");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetToken_EmptyToken_ReturnsError) {
    // Token must not be empty — whitespace-only input trims to empty.
    EXPECT_CALL(tokenStoreMock, storeToken(_)).Times(0);
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Token cannot be empty")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETTOKEN   ");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSetToken_TokenTooLong_ReturnsError) {
    // Token must be <= 64 chars.
    std::string longToken(65, 'X');
    EXPECT_CALL(tokenStoreMock, storeToken(_)).Times(0);
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Token too long")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSETTOKEN " + longToken);
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtClearWifi_ClearSuccess_ReturnsOkAndReboots) {
    // ATCLEARWIFI clears credentials and signals reboot+flush.
    EXPECT_CALL(credClearMock, clear()).WillOnce(Return(true));
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("OK WiFi credentials cleared")));
    EXPECT_CALL(tcpClientMock, flush()).Times(2);  // sendTcpPrompt + shouldFlushClient
    EXPECT_CALL(serialMock, println(::testing::StrEq("REBOOT")));
    EXPECT_CALL(serialMock, flush());
    EXPECT_CALL(espMock, restart());

    dispatcher->handleTcpCommand("ATCLEARWIFI");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtClearWifi_ClearFailure_ReturnsError) {
    // NVS clear failure propagates as an error — no reboot.
    EXPECT_CALL(credClearMock, clear()).WillOnce(Return(false));
    EXPECT_CALL(tcpClientMock, print(::testing::HasSubstr("ERROR Failed to clear credentials")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATCLEARWIFI");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtReboot_SetsRebootFlagWithoutExtraFlush) {
    // ATREBOOT returns shouldReboot=true but shouldFlushClient=false.
    // sendTcpPrompt already flushes, so the shouldFlushClient block must NOT
    // fire — an extra flush on a dead/half-closed socket would hang.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("REBOOT\r\r>")));
    EXPECT_CALL(tcpClientMock, flush()).Times(1);  // only from sendTcpPrompt
    EXPECT_CALL(serialMock, println(::testing::_)).Times(0);  // no REBOOT echo
    EXPECT_CALL(serialMock, flush()).Times(0);
    EXPECT_CALL(espMock, restart());

    dispatcher->handleTcpCommand("ATREBOOT");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtZ_DeactivatesMonitor) {
    // ATZ resets monitor state and returns the ELM327 banner.
    EXPECT_CALL(monitorMock, setMonitorActive(false));
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("ELM327 v2.3\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATZ");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtMa_ActivatesMonitor) {
    // ATMA turns monitor on.
    EXPECT_CALL(monitorMock, setMonitorActive(true));
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("OK\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATMA");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtPc_DeactivatesMonitor) {
    // ATPC turns monitor off.
    EXPECT_CALL(monitorMock, setMonitorActive(false));
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("OK\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATPC");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtHeLo_ReturnsHeloResponse) {
    // ATHELO echoes the device discovery banner including the device ID hex.
    EXPECT_CALL(tcpClientMock, print(::testing::AllOf(
        ::testing::HasSubstr("ACK DEVICE=ESP32-CAN-Bridge"),
        ::testing::HasSubstr("FIRMWARE=0.2.0"),
        ::testing::HasSubstr("DEVICEID=")
    )));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATHELO");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtE0_ReturnsOk) {
    // ATE0 (echo off) is a no-op acknowledgement.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("OK\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATE0");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtSp_ReturnsOk) {
    // ATSP (set protocol) is acknowledged without side effects.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("OK\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATSP0");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtH0_ReturnsOk) {
    // ATH0 (headers off) is acknowledged.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("OK\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATH0");
}

TEST_F(AtCommandDispatcherTest, HandleTcpCommand_AtCsm0_ReturnsOk) {
    // ATCSM0 (CSM off) is acknowledged.
    EXPECT_CALL(tcpClientMock, print(::testing::StrEq("OK\r\r>")));
    EXPECT_CALL(tcpClientMock, flush());
    EXPECT_CALL(espMock, restart()).Times(0);

    dispatcher->handleTcpCommand("ATCSM0");
}
