// TcpServerManager_test.cpp - Blind spec-first tests for the vanilla TCP
// accept/auth/dispatch state machine extracted from can-bridge.ino (Stage 6).
//
// BLIND DISCIPLINE: tests are derived from the HEADER CONTRACT ONLY
// (ITcpServer.h, TcpServerManager.h, WiFiManager.h's IStatusLED/WiFiState,
// StatusLED.h's Pattern enum). The .cpp bodies are RED-by-design stubs at
// the time these tests were written; ta-blind has NOT read TcpServerManager.cpp
// or can-bridge.ino.
//
// cycle() is VOID. All outcomes are observed via mock expectations on the four
// DI seams (ITcpServer, ITcpServerClient, ITcpHostCallbacks, IStatusLED). There
// are NO return-value assertions on cycle().

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "vanilla/TcpServerManager.h"
#include "vanilla/ITcpServer.h"
#include "vanilla/WiFiManager.h"   // IStatusLED, WiFiState::State
#include "vanilla/StatusLED.h"     // firmware::StatusLED::Pattern

#include <memory>
#include <string>
#include <vector>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Invoke;
using ::testing::InSequence;
using ::testing::NotNull;
using ::testing::IsNull;

namespace esp32_firmware {
namespace {

// ── Mocks for the four DI seams ──────────────────────────────────────────────

class MockTcpServerClient : public ITcpServerClient {
public:
    MOCK_METHOD(bool, connected, (), (const, override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(void, setTimeout, (uint32_t ms), (override));
    MOCK_METHOD(int, available, (), (const, override));
    MOCK_METHOD(std::string, readLine, (char delimiter), (override));
    MOCK_METHOD(std::string, readAvailableLine, (char delimiter), (override));
    MOCK_METHOD(void, println, (const std::string& line), (override));
    MOCK_METHOD(void, flush, (), (override));
    MOCK_METHOD(void, setNoDelay, (bool enable), (override));
    MOCK_METHOD(std::string, remoteIP, (), (const, override));
};

class MockTcpServer : public ITcpServer {
public:
    // accept() yields the next queued fake client (or nullptr when empty).
    // This lets a test script a sequence of accepts across multiple cycle()
    // calls without re-seeding expectations each time.
    MOCK_METHOD(std::unique_ptr<ITcpServerClient>, accept, (), (override));
    MOCK_METHOD(void, begin, (), (override));
    MOCK_METHOD(void, end, (), (override));

    // Test-owned clients handed out by accept(). They outlive the unique_ptr
    // the manager receives so the test can keep setting expectations.
    void queueAccept(std::unique_ptr<MockTcpServerClient> client) {
        queued_.push_back(std::move(client));
    }
    std::vector<std::unique_ptr<MockTcpServerClient>> queued_;
};

class MockTcpHostCallbacks : public ITcpHostCallbacks {
public:
    MOCK_METHOD(void, handleTcpAtCommand, (const std::string& cmd), (override));
    MOCK_METHOD(void, setMonitorActive, (bool active), (override));
    MOCK_METHOD(void, resetDiscoveryBackoff, (), (override));
    MOCK_METHOD(int, getWiFiState, (), (const, override));
    MOCK_METHOD(void, onClientConnected, (const std::string& ip), (override));
    MOCK_METHOD(void, onAuthFailed, (const std::string& ip), (override));
    MOCK_METHOD(void, onClientDisconnected, (const std::string& ip, int reason), (override));
};

// WiFiState as int (ITcpHostCallbacks::getWiFiState returns int).
constexpr int kWifiDisconnected =
    static_cast<int>(WiFiState::State::WIFI_DISCONNECTED);

const std::string kAuthToken = "vehicle-sim-2026";
const std::string kValidAuthLine = "AUTH " + kAuthToken;

// ── Fixture ──────────────────────────────────────────────────────────────────
class TcpServerManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Loose-by-default so spec-irrelevant calls don't fail.
        // Tests add strict EXPECT_CALLs for the behaviour they pin.
        ON_CALL(host_, getWiFiState()).WillByDefault(Return(kWifiDisconnected));
    }

    // Hand a fresh connected client to the manager's next accept().
    MockTcpServerClient& queueConnectedClient() {
        auto client = std::make_unique<MockTcpServerClient>();
        MockTcpServerClient* raw = client.get();
        ON_CALL(*raw, connected()).WillByDefault(Return(true));
        ON_CALL(*raw, available()).WillByDefault(Return(0));
        server_.queueAccept(std::move(client));
        return *raw;
    }

    // EXPECT_CALL(server_, accept()).WillOnce(acceptQueued()) — pops and
    // returns the next queued client (ownership transfers to the manager).
    // The underlying MockTcpServerClient is kept alive by the test body via
    // the reference returned from queueConnectedClient().
    auto acceptQueued() {
        return [this]() -> std::unique_ptr<ITcpServerClient> {
            if (server_.queued_.empty()) return nullptr;
            // Move the front client out, then erase the now-moved-from slot so
            // the NEXT call returns the subsequent client (not a stale null).
            std::unique_ptr<ITcpServerClient> owned = std::move(server_.queued_.front());
            server_.queued_.erase(server_.queued_.begin());
            return owned;
        };
    }

    // EXPECT_CALL(server_, accept()).WillOnce(acceptNone()) — no pending client.
    auto acceptNone() {
        return []() -> std::unique_ptr<ITcpServerClient> { return nullptr; };
    }

    MockTcpServer server_;
    MockTcpHostCallbacks host_;
    TcpServerManager manager_{server_, []() -> const std::string& { return kAuthToken; }, host_};
};

// ════════════════════════════════════════════════════════════════════════════
// §1  isValidAuthToken — pure static helper. REAL impl, expected GREEN.
//    Contract: received == ("AUTH " + expected). Caller trims; the function
//    does NOT re-trim (pre-trimmed inputs here).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, IsValidAuthToken_ExactMatch_True) {
    EXPECT_TRUE(TcpServerManager::isValidAuthToken(kValidAuthLine, kAuthToken));
}

TEST_F(TcpServerManagerTest, IsValidAuthToken_MissingAuthPrefix_False) {
    // Token alone, no "AUTH " prefix.
    EXPECT_FALSE(TcpServerManager::isValidAuthToken(kAuthToken, kAuthToken));
}

TEST_F(TcpServerManagerTest, IsValidAuthToken_WrongToken_False) {
    EXPECT_FALSE(TcpServerManager::isValidAuthToken("AUTH wrong-token", kAuthToken));
}

TEST_F(TcpServerManagerTest, IsValidAuthToken_EmptyReceived_False) {
    EXPECT_FALSE(TcpServerManager::isValidAuthToken("", kAuthToken));
}

TEST_F(TcpServerManagerTest, IsValidAuthToken_CaseSensitive) {
    // Lower-cased prefix must not match.
    EXPECT_FALSE(TcpServerManager::isValidAuthToken("auth " + kAuthToken, kAuthToken));
}

// ════════════════════════════════════════════════════════════════════════════
// §2  ACCEPT — accept() yields a client whose first readLine is a valid AUTH.
//    Expected: println("OK"), then monitor AUTO-ACTIVATES.
//
//    CONTRACT (raw-protocol stream-on-connect): the host CLI connects over
//    tcp:// with protocol=raw and therefore never sends the ELM327 init
//    sequence (ATZ/ATSP6/ATH1/ATMA). An authenticated client is by definition
//    asking for the CAN stream, so the firmware activates the monitor itself
//    once AUTH succeeds. Without this the client sits connected and receives
//    zero frames — indistinguishable from a dead bus.
//
//    The accept-time clear to false is retained as the safe default while the
//    auth outcome is still unknown, so a stale monitor from a previous client
//    can never leak to an unauthenticated newcomer.
//
//    LED pattern is NOT set here — it is owned by FirmwareApp via
//    selectLedPattern(wifiState, clientConnected). FirmwareApp::update()
//    queries IClientConnectionSource (TcpServerManager::hasClient()) and will
//    show CLIENT_CONNECTED on the next loop tick.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_AcceptValidAuth_PrintsOkAndActivatesMonitor) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));

    // Order matters: the pre-auth clear must not undo the post-auth activation.
    {
        InSequence seq;
        EXPECT_CALL(host_, setMonitorActive(false));
        EXPECT_CALL(host_, setMonitorActive(true));
    }

    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());

    manager_.cycle(/*nowMs=*/1000);
}

TEST_F(TcpServerManagerTest, Cycle_AcceptValidAuth_MonitorEndsActive) {
    // Behavioural restatement of the above without ordering: whatever the
    // internal sequence, the LAST monitor transition of a successful AUTH
    // cycle must leave streaming ON. This is the assertion that actually
    // maps to "the host CLI receives CAN frames".
    bool monitorState = false;
    ON_CALL(host_, setMonitorActive(_))
        .WillByDefault(Invoke([&monitorState](bool active) { monitorState = active; }));

    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(host_, setMonitorActive(_)).Times(AnyNumber());
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());

    manager_.cycle(/*nowMs=*/1000);

    EXPECT_TRUE(monitorState)
        << "an authenticated raw-protocol client must be streaming without needing ATMA";
}

// Phase 1 latency: TCP_NODELAY must be requested on the adopted client at the
// instant of accept (before auth), so the CAN stream never stalls behind Nagle.
TEST_F(TcpServerManagerTest, Cycle_AcceptValidAuth_RequestsNoDelayOnClient) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(host_, setMonitorActive(_)).Times(AnyNumber());

    // The manager must disable Nagle on the stream client at accept.
    EXPECT_CALL(client, setNoDelay(true));

    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());

    manager_.cycle(/*nowMs=*/1000);
}

TEST_F(TcpServerManagerTest, Cycle_AcceptInvalidAuth_MonitorEndsInactive) {
    // Negative control for the auto-activation: a rejected client must NOT be
    // handed the CAN stream. Pins that auto-activate sits on the success branch
    // only.
    bool monitorState = true;  // deliberately hostile start
    ON_CALL(host_, setMonitorActive(_))
        .WillByDefault(Invoke([&monitorState](bool active) { monitorState = active; }));

    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string("AUTH wrong-token")));
    EXPECT_CALL(client, println(std::string("ERROR unauthorized")));
    EXPECT_CALL(client, flush());
    EXPECT_CALL(client, stop());
    EXPECT_CALL(host_, setMonitorActive(_)).Times(AnyNumber());
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());

    manager_.cycle(/*nowMs=*/1000);

    EXPECT_FALSE(monitorState)
        << "a rejected client must never be streamed CAN data";
}

// ════════════════════════════════════════════════════════════════════════════
// §3  REJECT — accept() yields a client whose first readLine is INVALID.
//    Expected: println("ERROR unauthorized"), flush, stop().
//    LED pattern is NOT set here — owned by FirmwareApp via selectLedPattern.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_AcceptInvalidAuth_PrintsErrorFlushesAndStops) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string("AUTH nope")));
    EXPECT_CALL(client, println(std::string("ERROR unauthorized")));
    EXPECT_CALL(client, flush());
    EXPECT_CALL(client, stop());
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());

    manager_.cycle(/*nowMs=*/1000);
}

// ════════════════════════════════════════════════════════════════════════════
// §4  CLIENT-REPLACE — an active client is live; accept() yields a NEW client.
//    Expected: the OLD client is stop()'d, the NEW one is adopted and proceeds
//    through auth. RED (cycle is a stub).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_NewClientReplacesActive_StopsOldAndAuthsNew) {
    // Seed the manager with an active client via a first valid-auth cycle.
    MockTcpServerClient& first = queueConnectedClient();
    EXPECT_CALL(first, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(first, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(first, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Second cycle: a NEW client arrives while `first` is still connected.
    MockTcpServerClient& next = queueConnectedClient();
    EXPECT_CALL(first, connected()).WillRepeatedly(Return(true)); // still live
    EXPECT_CALL(first, stop());                                   // replaced → stopped
    EXPECT_CALL(next, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(next, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(next, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/2000);
}

// ════════════════════════════════════════════════════════════════════════════
// §5  COMMAND mode — authenticated client, monitor NOT active, bytes available.
//    Expected: readLine → handleTcpAtCommand(cmd). RED (cycle is a stub).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_AuthenticatedClientWithCommand_ForwardsToHost) {
    // Bring up an authenticated client (valid AUTH first cycle).
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_))
        .WillOnce(Return(kValidAuthLine));  // auth line
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Second cycle: client connected, monitor inactive, a command is available.
    // The manager reads NON-BLOCKING (readAvailableLine) every tick regardless of
    // available(), returning the buffered bytes verbatim (delimiter retained);
    // the manager splits on '\r'.
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, readAvailableLine(_)).WillOnce(Return(std::string("AT+HELLO\r")));
    EXPECT_CALL(host_, handleTcpAtCommand(std::string("AT+HELLO")));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));  // no new client
    manager_.cycle(/*nowMs=*/2000);
}

// REGRESSION GUARD (wificoldboot RCA): on the ESP32 WiFiClient, available() can
// transiently return 0 even when bytes are buffered (e.g. right after the AUTH
// readStringUntil). A manager that gates the read on available()>0 would then
// SKIP the ready command forever, leaving TCP AT commands (ATI/ATHELO/PING)
// unanswered — which is exactly what broke the macOS client's HELO handshake.
// The manager must call readAvailableLine() UNCONDITIONALLY every tick and
// dispatch whatever it returns, NOT wait for available()>0.
TEST_F(TcpServerManagerTest, Cycle_CommandDispatchedEvenWhenAvailableReportsZero) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // available() lies and reports 0, but readAvailableLine() still returns the
    // buffered command. The manager must NOT gate on available() and must
    // dispatch the command regardless. (The manager no longer calls available()
    // at all — it reads unconditionally every tick.)
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, readAvailableLine(_)).WillOnce(Return(std::string("AT+HELLO\r")));
    EXPECT_CALL(host_, handleTcpAtCommand(std::string("AT+HELLO")));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

// PING keepalive must round-trip even when available() reports 0 (same quirk),
// because the manager reads unconditionally.
TEST_F(TcpServerManagerTest, Cycle_PingRepliedEvenWhenAvailableReportsZero) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    EXPECT_CALL(client, connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, readAvailableLine(_)).WillOnce(Return(std::string("PING 42\r")));
    EXPECT_CALL(client, println(std::string("PONG 42")));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

// Phase 1 TX-starvation fix: a PARTIALLY-arrived command (no delimiter buffered
// yet) must NOT block cycle() and must NOT dispatch. The manager retains the
// partial bytes and dispatches only once the delimiter arrives on a later tick.
TEST_F(TcpServerManagerTest, Cycle_PartialCommand_DoesNotDispatchOrBlock) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(host_, setMonitorActive(_)).Times(AnyNumber());
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Tick 2: only "AT+HE" has arrived (no '\r'). readAvailableLine returns it
    // verbatim; the manager must accumulate and NOT call handleTcpAtCommand.
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, readAvailableLine(_)).WillOnce(Return(std::string("AT+HE")));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);  // must return promptly, no dispatch

    // Tick 3: the rest arrives with the delimiter. Now the full command dispatches.
    EXPECT_CALL(client, readAvailableLine(_)).WillOnce(Return(std::string("LLO\r")));
    EXPECT_CALL(host_, handleTcpAtCommand(std::string("AT+HELLO")));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/3000);
}

// ════════════════════════════════════════════════════════════════════════════
// §6  DISCONNECT cleanup — client drops while monitor was active.
//    Expected: setMonitorActive(false), resetDiscoveryBackoff().
//    LED pattern is NOT set here — owned by FirmwareApp via selectLedPattern.
//    IClientConnectionSource (TcpServerManager::hasClient()) will report
//    clientConnected=false, and FirmwareApp::update() will select the correct
//    wifi-state pattern on the next loop tick.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_ClientDropsWhileConnectedSta_CleansUpMonitor) {
    // Establish an authenticated client first.
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Next cycle: client is gone (connected()==false).
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(host_, resetDiscoveryBackoff());
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

TEST_F(TcpServerManagerTest, Cycle_ClientDropsWhileConnectedAp_CleansUpMonitor) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(host_, resetDiscoveryBackoff());
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

TEST_F(TcpServerManagerTest, Cycle_ClientDropsWhileWifiDisconnected_CleansUpMonitor) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Disconnect while WiFi is also DISCONNECTED — still cleans up monitor.
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(host_, resetDiscoveryBackoff());
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

// ════════════════════════════════════════════════════════════════════════════
// §7  EMPTY-line auth — readLine returns "" on the first line → REJECT path
//    (§3 behaviour). LED pattern is NOT set here — owned by FirmwareApp.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_EmptyFirstLine_RejectsAsUnauthorized) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string("")));
    EXPECT_CALL(client, println(std::string("ERROR unauthorized")));
    EXPECT_CALL(client, flush());
    EXPECT_CALL(client, stop());
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);
}

// ════════════════════════════════════════════════════════════════════════════
// §8  NO pending connection — accept() returns nullptr.
//    Expected: cycle is a no-op (no println, no adoption, no monitor change).
//    LED pattern is NOT set here — owned by FirmwareApp via selectLedPattern.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_NoPendingClient_NoSideEffects) {
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    // No client mock is queued, so println/flush/stop expectations can't fire.
    // Forbid any monitor transition on an idle cycle.
    EXPECT_CALL(host_, setMonitorActive(_)).Times(0);
    manager_.cycle(/*nowMs=*/1000);
}

// ════════════════════════════════════════════════════════════════════════════
// §9  WHITESPACE first line — the wire delivered a blank/whitespace-only line
//    (e.g. a pre-auth keepalive CR). trim() reduces it to "" which cannot equal
//    "AUTH <token>", so the client is rejected as unauthorized. Pins trim()'s
//    all-whitespace → empty branch (an uncovered path) without asserting on the
//    private helper directly.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_AcceptBlankLine_RejectsAsUnauthorized) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    // Pure whitespace (CR/space/tab) — trim() collapses this to an empty line.
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string(" \r\t ")));
    EXPECT_CALL(client, println(std::string("ERROR unauthorized")));
    EXPECT_CALL(client, flush());
    EXPECT_CALL(client, stop());
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());

    manager_.cycle(/*nowMs=*/1000);
}

// ════════════════════════════════════════════════════════════════════════════
// §10 LIFECYCLE — start() is a reserved no-op (the listening socket is owned by
//     the .ino); calling it must not touch any seam. stop() drops an adopted
//     client: the held client is stop()'d and the handle released. Pins the
//     shutdown cleanup path (stop with an active client).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Start_IsNoOp_DoesNotTouchAnySeam) {
    // start() is intentionally a no-op: no accept/begin/monitor calls.
    // LED pattern is NOT set here — owned by FirmwareApp via selectLedPattern.
    EXPECT_CALL(server_, accept()).Times(0);
    EXPECT_CALL(server_, begin()).Times(0);
    EXPECT_CALL(host_, setMonitorActive(_)).Times(0);
    manager_.start();
}

TEST_F(TcpServerManagerTest, Stop_WithAdoptedClient_StopsAndReleasesClient) {
    // Adopt a client first (valid AUTH cycle installs it as current_). The
    // auth-adoption cycle drives println("OK") + setMonitorActive(false).
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // stop() must stop() the held client exactly once (shutdown teardown).
    // After stop(), current_ releases (deletes) the client, so no further
    // expectations are set on `client` — its handle is gone by design.
    EXPECT_CALL(client, stop()).Times(1);
    manager_.stop();
}

// ════════════════════════════════════════════════════════════════════════════
// §11 writeLineToClient — serve a formatted line (e.g. the [STATE] heartbeat)
//     to the adopted client. The .ino calls this after every heartbeat tick so
//     the CLI's --status works over TCP without a USB serial connection.
// ════════════════════════════════════════════════════════════════════════════

// CONTRACT: with a live adopted client, the line is forwarded verbatim.
TEST_F(TcpServerManagerTest, WriteLineToClient_WithLiveClient_ForwardsLine) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Client is live → the line is forwarded via println().
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, println(std::string("[STATE] uptime=5000ms wifi=WIFI_CONNECTED\r\n")));
    manager_.writeLineToClient("[STATE] uptime=5000ms wifi=WIFI_CONNECTED\r\n");
}

// CONTRACT: with NO adopted client, writeLineToClient is a silent no-op (no
// crash, no interaction with the server or host). No EXPECT_CALL on
// server_.accept() — writeLineToClient() never calls accept(); a stray
// expectation here (copy-pasted from a cycle() test) would never be satisfied.
TEST_F(TcpServerManagerTest, WriteLineToClient_NoClient_IsNoOp) {
    manager_.writeLineToClient("[STATE] uptime=5000ms wifi=WIFI_CONNECTED\r\n");
}

// CONTRACT: with an adopted client that has since dropped, writeLineToClient is
// a no-op (guards on connected(), not just handle non-null — a dead socket must
// not be written to, which would stall the loop).
TEST_F(TcpServerManagerTest, WriteLineToClient_DroppedClient_IsNoOp) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Client handle still non-null (hasClient() true) but connected()==false.
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(client, println(_)).Times(0);
    manager_.writeLineToClient("[STATE] uptime=5000ms wifi=WIFI_CONNECTED\r\n");
}

}  // namespace
}  // namespace esp32_firmware
