// WiFiManager_SerialTrace_test.cpp - Contract tests for the ISerial debug-trace
// seam on WiFiManager.
//
// CONTRACT UNDER TEST: WiFiManager emits a human-readable [STATE] trace line for
// every WiFi state transition, and the line carries the REASON when the
// transition was driven by a disconnect event. Three emission points:
//
//   1. onDisconnected() with a definitive-auth reason  -> "... -> WIFI_AP_MODE (auth fail reason=N)"
//   2. onDisconnected() with a transient reason        -> "... -> RECONNECTING (reason=N)"
//   3. applyStateTransition() (state-machine driven)   -> "<from> -> <to>"
//
// These tests assert on the RENDERED line content (state names + reason code),
// not on exact whitespace-for-whitespace equality of the whole string, so the
// format can be retuned without breaking the suite. What is locked down is the
// information content: which states, and which reason code.

#include <gtest/gtest.h>

#include "vanilla/WiFiManager.h"
#include "vanilla/WiFiReasonCodes.h"
#include "mocks/WiFiMock.h"
#include "mocks/PreferencesMock.h"
#include "mocks/SerialDebugMock.h"

namespace esp32_firmware {
namespace {

class WiFiManagerSerialTraceTest : public ::testing::Test {
protected:
    WiFiMock wifiMock;
    PreferencesMock prefsMock;
    SerialDebugMock serialMock;
    std::unique_ptr<WiFiManager> wifiManager;

    void SetUp() override {
        wifiMock.reset();
        prefsMock.reset();
        serialMock.reset();
        wifiManager = std::make_unique<WiFiManager>(
            wifiMock, prefsMock, serialMock, "baked-ssid", "baked-pass");
    }

    // Drive the manager to WIFI_CONNECTED, then clear the trace so each test
    // asserts only on the lines its own scenario produced.
    // NOTE: the status must be re-armed AFTER init(), because init() issues
    // WiFi.begin(), and WiFiMock::begin() resets status to WL_IDLE_STATUS.
    void driveToConnectedAndClearTrace() {
        wifiManager->init();
        ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
        wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
        wifiManager->update(1000);
        ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
        serialMock.reset();
    }
};

// ── 1. Definitive auth failure → AP mode ─────────────────────────────────────

TEST_F(WiFiManagerSerialTraceTest, AuthFailure_LogsTransitionToApModeWithReason) {
    driveToConnectedAndClearTrace();

    wifiManager->onDisconnected(WIFI_REASON_AUTH_FAIL);

    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    ASSERT_EQ(serialMock.lines().size(), 1u);
    const std::string line = serialMock.firstLine();
    EXPECT_NE(line.find("WIFI_CONNECTED"), std::string::npos) << line;
    EXPECT_NE(line.find("WIFI_AP_MODE"), std::string::npos) << line;
    EXPECT_NE(line.find("reason=202"), std::string::npos) << line;
}

TEST_F(WiFiManagerSerialTraceTest, AuthFailure_LogsTheActualReasonCode_NotHardcoded) {
    // The reason in the trace must be the code that actually drove the
    // escalation — a hardcoded value would pass the test above but fail here.
    driveToConnectedAndClearTrace();

    wifiManager->onDisconnected(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);

    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_NE(serialMock.firstLine().find("reason=15"), std::string::npos) << serialMock.firstLine();
}

TEST_F(WiFiManagerSerialTraceTest, AuthFailure_TraceReportsTrueOriginState) {
    // Escalating from WIFI_CONNECTING (not WIFI_CONNECTED) must report
    // WIFI_CONNECTING as the from-state — proving the origin is captured before
    // the state is mutated rather than read back afterwards.
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    serialMock.reset();

    wifiManager->onDisconnected(WIFI_REASON_802_1X_AUTH_FAILED);

    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    const std::string line = serialMock.firstLine();
    EXPECT_NE(line.find("WIFI_CONNECTING"), std::string::npos) << line;
    EXPECT_NE(line.find("WIFI_AP_MODE"), std::string::npos) << line;
    EXPECT_NE(line.find("reason=23"), std::string::npos) << line;
}

// ── 2. Transient disconnect → RECONNECTING ───────────────────────────────────

TEST_F(WiFiManagerSerialTraceTest, TransientDisconnect_LogsReconnectingWithReason) {
    driveToConnectedAndClearTrace();

    wifiManager->onDisconnected(WIFI_REASON_BEACON_TIMEOUT);

    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    ASSERT_EQ(serialMock.lines().size(), 1u);
    const std::string line = serialMock.firstLine();
    EXPECT_NE(line.find("WIFI_CONNECTED"), std::string::npos) << line;
    EXPECT_NE(line.find("reason=200"), std::string::npos) << line;
}

TEST_F(WiFiManagerSerialTraceTest, TransientDisconnect_NotConnected_LogsNothing) {
    // A transient disconnect while already disconnected changes no state, so it
    // must not emit a transition trace — silence is the contract for a no-op.
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    serialMock.reset();

    wifiManager->onDisconnected(WIFI_REASON_ASSOC_EXPIRE);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(serialMock.lines().empty());
}

// ── 3. State-machine-driven transitions ──────────────────────────────────────

TEST_F(WiFiManagerSerialTraceTest, StateMachineTransition_LogsFromAndToStates) {
    wifiManager->init();  // WIFI_DISCONNECTED -> WIFI_CONNECTING

    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    ASSERT_FALSE(serialMock.lines().empty());
    const std::string line = serialMock.firstLine();
    EXPECT_NE(line.find("WIFI_DISCONNECTED"), std::string::npos) << line;
    EXPECT_NE(line.find("WIFI_CONNECTING"), std::string::npos) << line;
}

TEST_F(WiFiManagerSerialTraceTest, ConnectSuccess_LogsConnectingToConnected) {
    wifiManager->init();
    serialMock.reset();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);

    wifiManager->update(1000);  // WIFI_CONNECTING -> WIFI_CONNECTED

    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    ASSERT_EQ(serialMock.lines().size(), 1u);
    const std::string line = serialMock.firstLine();
    EXPECT_NE(line.find("WIFI_CONNECTING"), std::string::npos) << line;
    EXPECT_NE(line.find("WIFI_CONNECTED"), std::string::npos) << line;
}

TEST_F(WiFiManagerSerialTraceTest, SteadyState_EmitsNoRepeatedTraceLines) {
    // The trace is per-TRANSITION, not per-tick. Idling in a stable state must
    // stay silent, otherwise the serial console floods every loop iteration.
    driveToConnectedAndClearTrace();

    wifiManager->update(2000);
    wifiManager->update(3000);
    wifiManager->update(4000);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTED);
    EXPECT_TRUE(serialMock.lines().empty());
}

TEST_F(WiFiManagerSerialTraceTest, EveryTraceLineIsAStateLine) {
    // All trace output flows through the [STATE] channel so the console can be
    // filtered on that prefix.
    wifiManager->init();
    wifiMock.setStatus(WiFiMock::Status::WL_CONNECTED);
    wifiManager->update(1000);
    wifiManager->onDisconnected(WIFI_REASON_BEACON_TIMEOUT);

    ASSERT_FALSE(serialMock.lines().empty());
    for (const std::string& line : serialMock.lines()) {
        EXPECT_EQ(line.rfind("[STATE]", 0), 0u) << line;
    }
}

// ── 4. Silence contracts (no-op transitions must not flood the console) ──────

TEST_F(WiFiManagerSerialTraceTest, ConnectingRetryTicks_EmitNoTraceLines) {
    // The real serial-flood risk: stuck in WIFI_CONNECTING while the connection
    // never completes. Each retry tick re-issues begin(), but the STATE is
    // unchanged, so the trace must stay silent — a per-tick [STATE] line on a
    // no-op retry would flood the console every loop iteration.
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    serialMock.reset();

    const uint32_t retryInterval = WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    wifiMock.setStatus(WiFiMock::Status::WL_IDLE_STATUS);  // connection never completes
    wifiManager->update(retryInterval + 1);
    wifiManager->update(2 * retryInterval + 1);
    wifiManager->update(3 * retryInterval + 1);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_CONNECTING);
    EXPECT_TRUE(serialMock.lines().empty());
}

TEST_F(WiFiManagerSerialTraceTest, TransientDisconnectWhileInApMode_StaysSilent) {
    // A transient disconnect event delivered while already in WIFI_AP_MODE is a
    // no-op: AP mode is stable and ignores STA lifecycle events, so it must not
    // emit a transition trace. (No stored/baked credentials -> AP mode on init.)
    wifiManager = std::make_unique<WiFiManager>(
        wifiMock, prefsMock, serialMock, nullptr, nullptr);
    wifiManager->init();
    ASSERT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    serialMock.reset();

    wifiManager->onDisconnected(WIFI_REASON_BEACON_TIMEOUT);

    EXPECT_EQ(wifiManager->getState(), WiFiState::State::WIFI_AP_MODE);
    EXPECT_TRUE(serialMock.lines().empty());
}

} // namespace
} // namespace esp32_firmware
