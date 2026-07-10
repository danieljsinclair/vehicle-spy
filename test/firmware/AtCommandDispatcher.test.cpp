#include <gtest/gtest.h>
#include "firmware/vanilla/AtCommandDispatcher.h"

#include <array>
#include <string>

using namespace esp32_firmware;

namespace {

// ── Fakes for the five runtime boundaries ─────────────────────────────────────
class FakeTcpClient : public ITcpClientAt {
public:
    std::string lastPrinted;
    int printCalls = 0;
    int flushCalls = 0;
    void print(const char* str) override { lastPrinted = str; ++printCalls; }
    void flush() override { ++flushCalls; }
};

class FakeSerial : public ISerialAt {
public:
    std::string lastLine;
    int printlnCalls = 0;
    int flushCalls = 0;
    void println(const char* str) override { lastLine = str; ++printlnCalls; }
    void flush() override { ++flushCalls; }
};

class FakeEsp : public IEspAt {
public:
    int restartCalls = 0;
    void restart() override { ++restartCalls; }
};

class FakeWifiStore : public IWifiCredentialStore {
public:
    bool nextStoreResult = true;
    std::string lastSsid;
    std::string lastPassword;
    int storeCalls = 0;
    bool store(const std::string& ssid, const std::string& password) override {
        ++storeCalls;
        lastSsid = ssid;
        lastPassword = password;
        return nextStoreResult;
    }
};

class FakeMonitor : public IMonitorState {
public:
    bool active = false;
    void setMonitorActive(bool a) override { active = a; }
};

constexpr std::array<uint8_t, 16> kDeviceId = {
    0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
};

// Build a fully-wired dispatcher for command-handling tests.
struct DispatcherHarness {
    FakeTcpClient tcp;
    FakeSerial serial;
    FakeEsp esp;
    FakeWifiStore wifi;
    FakeMonitor monitor;
    AtCommandDispatcher d;

    DispatcherHarness()
        : d(tcp, serial, esp, wifi, monitor, kDeviceId) {}
};

// ── Pure-function tests ───────────────────────────────────────────────────────

TEST(AtCommandPureTest, NormalizeTrimsAndUpperCases) {
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("  atz \r\n"), "ATZ");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("atsp6"), "ATSP6");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand(""), "");
    EXPECT_EQ(AtCommandDispatcher::normalizeAtCommand("  "), "");
}

TEST(AtCommandPureTest, BuildHeloResponseFormatsDeviceId) {
    std::string helo = AtCommandDispatcher::buildHeloResponse(kDeviceId, "ESP32-CAN-Bridge", "0.2.0");
    EXPECT_EQ(helo,
        "ACK DEVICE=ESP32-CAN-Bridge FIRMWARE=0.2.0 DEVICEID="
        "DEADBEEF000102030405060708090A0B\r");
}

TEST(AtCommandPureTest, ParseSetWifiParamsHappyPath) {
    auto p = AtCommandDispatcher::parseSetWifiParams("MyNet,secret123");
    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.ssid, "MyNet");
    EXPECT_EQ(p.password, "secret123");
}

TEST(AtCommandPureTest, ParseSetWifiParamsRejectsMissingComma) {
    EXPECT_FALSE(AtCommandDispatcher::parseSetWifiParams("NoCommaHere").valid);
}

TEST(AtCommandPureTest, ParseSetWifiParamsRejectsLeadingComma) {
    EXPECT_FALSE(AtCommandDispatcher::parseSetWifiParams(",onlypass").valid);
}

TEST(AtCommandPureTest, IsValidAuthTokenMatchesExpectedPrefix) {
    EXPECT_TRUE(AtCommandDispatcher::isValidAuthToken("AUTH vehicle-sim-2026", "vehicle-sim-2026"));
    EXPECT_FALSE(AtCommandDispatcher::isValidAuthToken("AUTH wrong", "vehicle-sim-2026"));
    EXPECT_FALSE(AtCommandDispatcher::isValidAuthToken("vehicle-sim-2026", "vehicle-sim-2026"));
}

// ── Command-dispatch tests ────────────────────────────────────────────────────

TEST(AtCommandDispatchTest, AtzReturnsElmVersionAndClearsMonitor) {
    DispatcherHarness h;
    h.monitor.active = true;
    h.d.handleSerialCommand("atz");
    EXPECT_EQ(h.serial.lastLine, "ELM327 v2.3");
    EXPECT_FALSE(h.monitor.active);
    EXPECT_EQ(h.esp.restartCalls, 0);
}

TEST(AtCommandDispatchTest, AteRespondsOk) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATE1");
    EXPECT_EQ(h.serial.lastLine, "OK");
}

TEST(AtCommandDispatchTest, AtspPrefixMatches) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATSP6");
    EXPECT_EQ(h.serial.lastLine, "OK");
}

TEST(AtCommandDispatchTest, AthRespondsOk) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATH1");
    EXPECT_EQ(h.serial.lastLine, "OK");
}

TEST(AtCommandDispatchTest, AtcsmRespondsOk) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATCSM0");
    EXPECT_EQ(h.serial.lastLine, "OK");
}

TEST(AtCommandDispatchTest, AtmaActivatesMonitor) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATMA");
    EXPECT_EQ(h.serial.lastLine, "OK");
    EXPECT_TRUE(h.monitor.active);
}

TEST(AtCommandDispatchTest, AtpcDeactivatesMonitor) {
    DispatcherHarness h;
    h.monitor.active = true;
    h.d.handleSerialCommand("ATPC");
    EXPECT_EQ(h.serial.lastLine, "OK");
    EXPECT_FALSE(h.monitor.active);
}

TEST(AtCommandDispatchTest, AtheloReturnsDeviceId) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATHELO");
    EXPECT_EQ(h.serial.lastLine,
        "ACK DEVICE=ESP32-CAN-Bridge FIRMWARE=0.2.0 DEVICEID="
        "DEADBEEF000102030405060708090A0B\r");
}

TEST(AtCommandDispatchTest, AtiReturnsDeviceInfo) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATI");
    EXPECT_EQ(h.serial.lastLine, "ESP32 CAN Bridge v0.1");
}

// The TCP prompt path must frame the reply as "<response>\r\r>" to the TCP
// client ONLY — the host's TCPTransport::sendHeloAndParseAck waits for the
// "\r\r>" terminator to complete the HELO handshake. The serial console must
// NOT be echoed on the TCP path (that belongs to the serial command path).
TEST(AtCommandDispatchTest, TcpPromptFramesResponseWithCrCrGtAndIsClientOnly) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATI");
    EXPECT_EQ(h.tcp.lastPrinted, "ESP32 CAN Bridge v0.1\r\r>");
    EXPECT_EQ(h.serial.printlnCalls, 0);
}

TEST(AtCommandDispatchTest, AtheloTcpPromptCarriesTerminatorForHostHandshake) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATHELO");
    // buildHeloResponse() ends in "\r"; the TCP prompt appends "\r\r>", yielding
    // "...\r\r\r>". The host's TCPTransport::sendHeloAndParseAck scans for the
    // "\r\r>" terminator, so the extra trailing "\r" is harmless (and matches what
    // the original device produced via client.printf("%s\r\r>", response)).
    EXPECT_EQ(h.tcp.lastPrinted,
        "ACK DEVICE=ESP32-CAN-Bridge FIRMWARE=0.2.0 DEVICEID="
        "DEADBEEF000102030405060708090A0B\r\r\r>");
    EXPECT_EQ(h.serial.printlnCalls, 0);
}

TEST(AtCommandDispatchTest, AtrebootTriggersRestartWithoutExtraClientFlush) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATREBOOT");
    // ATREBOOT echoes "REBOOT" via the TCP prompt (framed as "REBOOT\r\r>") with a
    // single flush, but its result sets shouldFlushClient=false, so the dispatcher's
    // extra client.flush() side-effect block is BYPASSED. This is the fix for the
    // flush-hang bug: the prompt flush is the only flush, then ESP.restart() proceeds.
    // We assert exactly one flush (from the prompt) and exactly one restart. The
    // serial console is NOT echoed on the TCP path.
    EXPECT_EQ(h.esp.restartCalls, 1);
    EXPECT_EQ(h.tcp.flushCalls, 1);
    EXPECT_EQ(h.tcp.lastPrinted, "REBOOT\r\r>");
    EXPECT_EQ(h.serial.printlnCalls, 0);
}

TEST(AtCommandDispatchTest, UnknownCommandReturnsQuestionMark) {
    DispatcherHarness h;
    h.d.handleSerialCommand("ATWAT");
    EXPECT_EQ(h.serial.lastLine, "?");
    EXPECT_EQ(h.esp.restartCalls, 0);
}

TEST(AtCommandDispatchTest, NormalizationAppliedBeforeMatch) {
    DispatcherHarness h;
    h.d.handleSerialCommand("  atz \r\n");
    EXPECT_EQ(h.serial.lastLine, "ELM327 v2.3");
}

// ── ATSETWIFI tests ────────────────────────────────────────────────────────────

TEST(AtCommandSetWifiTest, StoresValidCredentialsAndRebootsWithFlush) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATSETWIFI MyNet,secret123");
    EXPECT_EQ(h.wifi.storeCalls, 1);
    EXPECT_EQ(h.wifi.lastSsid, "MyNet");
    EXPECT_EQ(h.wifi.lastPassword, "secret123");
    // The success prompt is written to the TCP client, framed as "...\r\r>"...
    EXPECT_EQ(h.tcp.lastPrinted, "OK WiFi credentials stored. Rebooting to connect...\r\r>");
    // ...and the flush-path side-effect block then prints "REBOOT" to serial last
    // (faithful to the .ino: Serial.println("REBOOT") after the prompt).
    EXPECT_EQ(h.serial.lastLine, "REBOOT");
    EXPECT_EQ(h.esp.restartCalls, 1);
    // ATSETWIFI sets shouldFlushClient=true -> dispatcher extra client.flush()
    // (the sendPrompt already flushed once, then the side-effect block flushes again).
    EXPECT_EQ(h.tcp.flushCalls, 2);
}

TEST(AtCommandSetWifiTest, RejectsMissingComma) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATSETWIFI nocomma");
    EXPECT_EQ(h.wifi.storeCalls, 0);
    // The error goes to the TCP client (framed), not the serial console.
    EXPECT_EQ(h.tcp.lastPrinted, "ERROR Invalid format. Use: ATSETWIFI<ssid>,<pass>\r\r>");
    EXPECT_EQ(h.esp.restartCalls, 0);
}

TEST(AtCommandSetWifiTest, RejectsEmptySsid) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATSETWIFI ,pass");
    EXPECT_EQ(h.wifi.storeCalls, 0);
    // A leading comma means commaIndex==0, which parseSetWifiParams treats as an
    // invalid *format* (not a length error) — so the format error fires first.
    // This mirrors the .ino ordering exactly. The error is framed to the TCP client.
    EXPECT_EQ(h.tcp.lastPrinted, "ERROR Invalid format. Use: ATSETWIFI<ssid>,<pass>\r\r>");
}

TEST(AtCommandSetWifiTest, RejectsOverlongSsid) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATSETWIFI " + std::string(33, 'x') + ",pass");
    EXPECT_EQ(h.wifi.storeCalls, 0);
    EXPECT_EQ(h.tcp.lastPrinted, "ERROR Invalid SSID length (1-32 chars)\r\r>");
}

TEST(AtCommandSetWifiTest, RejectsOverlongPassword) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATSETWIFI net," + std::string(65, 'y'));
    EXPECT_EQ(h.wifi.storeCalls, 0);
    EXPECT_EQ(h.tcp.lastPrinted, "ERROR Invalid password length (1-64 chars)\r\r>");
}

TEST(AtCommandSetWifiTest, ReportsStoreFailure) {
    DispatcherHarness h;
    h.wifi.nextStoreResult = false;
    h.d.handleTcpCommand("ATSETWIFI net,pass");
    EXPECT_EQ(h.wifi.storeCalls, 1);
    EXPECT_EQ(h.tcp.lastPrinted, "ERROR Failed to store credentials\r\r>");
    EXPECT_EQ(h.esp.restartCalls, 0);
}

TEST(AtCommandSetWifiTest, TrimsOnlyOuterWhitespaceNotInner) {
    DispatcherHarness h;
    h.d.handleTcpCommand("ATSETWIFI   net  ,  pass  ");
    EXPECT_EQ(h.wifi.storeCalls, 1);
    // Only the outer whitespace of the whole param string is trimmed (mirrors the
    // .ino's params.trim()); whitespace adjacent to the comma is preserved verbatim.
    EXPECT_EQ(h.wifi.lastSsid, "net  ");
    EXPECT_EQ(h.wifi.lastPassword, "  pass");
}

} // namespace
