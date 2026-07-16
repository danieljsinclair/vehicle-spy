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
    MOCK_METHOD(void, println, (const std::string& line), (override));
    MOCK_METHOD(void, flush, (), (override));
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
};

class MockStatusLED : public IStatusLED {
public:
    MOCK_METHOD(void, setPattern, (int pattern), (override));
    MOCK_METHOD(void, update, (uint32_t now), (override));
};

// ── Pattern constants (from firmware::StatusLED::Pattern, no magic numbers) ──
// IStatusLED::setPattern takes a plain int; the manager passes a Pattern enum
// value. We assert against the declared enum ordinals so the test stays
// faithful to the contract without hard-coding literals.
constexpr int kLedClientConnected =
    static_cast<int>(firmware::StatusLED::Pattern::CLIENT_CONNECTED);
constexpr int kLedWifiConnected =
    static_cast<int>(firmware::StatusLED::Pattern::WIFI_CONNECTED);

// WiFiState as int (ITcpHostCallbacks::getWiFiState returns int).
constexpr int kWifiDisconnected =
    static_cast<int>(WiFiState::State::DISCONNECTED);
constexpr int kWifiConnectedSta =
    static_cast<int>(WiFiState::State::CONNECTED_STA);
constexpr int kWifiConnectedAp =
    static_cast<int>(WiFiState::State::CONNECTED_AP);

const std::string kAuthToken = "vehicle-sim-2026";
const std::string kValidAuthLine = "AUTH " + kAuthToken;

// ── Fixture ──────────────────────────────────────────────────────────────────
class TcpServerManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Loose-by-default so spec-irrelevant calls (e.g. update()) don't fail.
        // Tests add strict EXPECT_CALLs for the behaviour they pin.
        ON_CALL(led_, update(_)).WillByDefault(Return());
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
    MockStatusLED led_;
    MockTcpHostCallbacks host_;
    TcpServerManager manager_{server_, led_, kAuthToken, host_};
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
//    Expected: println("OK"), setPattern(CLIENT_CONNECTED), setMonitorActive(false).
//    RED (cycle is a stub).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_AcceptValidAuth_PrintsOkAndClientLedAndClearsMonitor) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(led_, setPattern(kLedClientConnected));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(server_, accept()).WillOnce([&](void) {
        // Hand back the one queued client.
        return std::unique_ptr<ITcpServerClient>(server_.queued_.empty()
            ? nullptr : server_.queued_.back().release());
    });

    manager_.cycle(/*nowMs=*/1000);
}

// ════════════════════════════════════════════════════════════════════════════
// §3  REJECT — accept() yields a client whose first readLine is INVALID.
//    Expected: println("ERROR unauthorized"), flush, stop(); NO CLIENT_CONNECTED.
//    RED (cycle is a stub).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_AcceptInvalidAuth_PrintsErrorFlushesAndStops) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string("AUTH nope")));
    EXPECT_CALL(client, println(std::string("ERROR unauthorized")));
    EXPECT_CALL(client, flush());
    EXPECT_CALL(client, stop());
    // Explicitly forbid the accept-success LED transition.
    EXPECT_CALL(led_, setPattern(kLedClientConnected)).Times(0);
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
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, available()).WillOnce(Return(4));  // bytes waiting
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string("AT+HELLO")));
    EXPECT_CALL(host_, handleTcpAtCommand(std::string("AT+HELLO")));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));  // no new client
    manager_.cycle(/*nowMs=*/2000);
}

// ════════════════════════════════════════════════════════════════════════════
// §6  DISCONNECT + LED revert — client drops while monitor was active.
//    Expected: setMonitorActive(false), resetDiscoveryBackoff(), and when WiFi
//    is CONNECTED_STA/CONNECTED_AP → setPattern(WIFI_CONNECTED).
//    When WiFi is DISCONNECTED → NO WIFI_CONNECTED revert.
//    RED (cycle is a stub).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_ClientDropsWhileConnectedSta_RevertsWifiLed) {
    // Establish an authenticated client first.
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Next cycle: client is gone (connected()==false) and WiFi is CONNECTED_STA.
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(host_, getWiFiState()).WillRepeatedly(Return(kWifiConnectedSta));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(host_, resetDiscoveryBackoff());
    EXPECT_CALL(led_, setPattern(kLedWifiConnected));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

TEST_F(TcpServerManagerTest, Cycle_ClientDropsWhileConnectedAp_RevertsWifiLed) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(host_, getWiFiState()).WillRepeatedly(Return(kWifiConnectedAp));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(host_, resetDiscoveryBackoff());
    EXPECT_CALL(led_, setPattern(kLedWifiConnected));
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

TEST_F(TcpServerManagerTest, Cycle_ClientDropsWhileWifiDisconnected_NoWifiLedRevert) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(kValidAuthLine));
    EXPECT_CALL(client, println(std::string("OK")));
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);

    // Disconnect while WiFi is also DISCONNECTED → no WIFI_CONNECTED revert.
    EXPECT_CALL(client, connected()).WillRepeatedly(Return(false));
    EXPECT_CALL(host_, getWiFiState()).WillRepeatedly(Return(kWifiDisconnected));
    EXPECT_CALL(host_, setMonitorActive(false));
    EXPECT_CALL(host_, resetDiscoveryBackoff());
    EXPECT_CALL(led_, setPattern(kLedWifiConnected)).Times(0);
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    manager_.cycle(/*nowMs=*/2000);
}

// ════════════════════════════════════════════════════════════════════════════
// §7  EMPTY-line auth — readLine returns "" on the first line → REJECT path
//    (§3 behaviour). RED (cycle is a stub).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_EmptyFirstLine_RejectsAsUnauthorized) {
    MockTcpServerClient& client = queueConnectedClient();
    EXPECT_CALL(client, setTimeout(_)).Times(AnyNumber());
    EXPECT_CALL(client, readLine(_)).WillOnce(Return(std::string("")));
    EXPECT_CALL(client, println(std::string("ERROR unauthorized")));
    EXPECT_CALL(client, flush());
    EXPECT_CALL(client, stop());
    EXPECT_CALL(led_, setPattern(kLedClientConnected)).Times(0);
    EXPECT_CALL(server_, accept()).WillOnce(acceptQueued());
    manager_.cycle(/*nowMs=*/1000);
}

// ════════════════════════════════════════════════════════════════════════════
// §8  NO pending connection — accept() returns nullptr.
//    Expected: cycle is a no-op (no println, no setPattern, no adoption).
//    RED (cycle is a stub — though a stub no-op is consistent with this; the
//    test pins that an empty accept must not spuriously drive any seam).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(TcpServerManagerTest, Cycle_NoPendingClient_NoSideEffects) {
    EXPECT_CALL(server_, accept()).WillOnce(Return(nullptr));
    // No client mock is queued, so println/flush/stop expectations can't fire.
    // Forbid any LED transition on an idle cycle.
    EXPECT_CALL(led_, setPattern(_)).Times(0);
    EXPECT_CALL(host_, setMonitorActive(_)).Times(0);
    manager_.cycle(/*nowMs=*/1000);
}

}  // namespace
}  // namespace esp32_firmware
