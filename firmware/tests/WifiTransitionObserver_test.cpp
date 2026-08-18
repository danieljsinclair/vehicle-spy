// WifiTransitionObserver_test.cpp - unit tests for the [EVENT] emission policy
// over WiFi state transitions (a listener over the state model).
//
// Pins the CONTRACT:
//   - Entering WIFI_CONNECTED emits wifi_connected with the local IP.
//   - Leaving WIFI_CONNECTED emits wifi_drop with the last recorded reason.
//   - Entering EITHER AP state (DEFAULT or AUTH_FAIL) from a non-AP state emits
//     wifi_ap_fallback with the manager-recorded escalation reason.
//   - No transition (same state) emits nothing.
//   - A successful connect clears the recorded drop reason.

#include "WifiTransitionObserver.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <vector>

using esp32_firmware::ITransitionEventSink;
using esp32_firmware::WifiTransitionObserver;

namespace {

// Recording sink: captures (type, detail) pairs so tests assert business intent
// (which events fired, with what key data) rather than mock call ordering.
class RecordingSink : public ITransitionEventSink {
public:
    void onTransitionEvent(const char* eventType, const std::string& detail) override {
        events_.push_back(std::string(eventType) + "|" + detail);
    }
    const std::vector<std::string>& events() const { return events_; }
    void clear() { events_.clear(); }

private:
    std::vector<std::string> events_;
};

namespace {

TEST(WifiTransitionObserverTest, ConnectingToConnectedEmitsWifiConnectedWithIp) {
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING));

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTED),
                     "192.168.1.50", 0);

    ASSERT_EQ(sink.events().size(), 1u);
    EXPECT_EQ(sink.events()[0], "wifi_connected|ip=192.168.1.50");
}

TEST(WifiTransitionObserverTest, ConnectedToConnectingEmitsWifiDropWithRecordedReason) {
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTED));
    observer.recordDisconnectReason(200);  // BEACON_TIMEOUT

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING),
                     "", 0);

    ASSERT_EQ(sink.events().size(), 1u);
    EXPECT_EQ(sink.events()[0], "wifi_drop|reason=200");
}

TEST(WifiTransitionObserverTest, EnteringApDefaultFromNonApEmitsApFallback) {
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_DISCONNECTED));

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_AP_MODE_DEFAULT),
                     "", 0);

    ASSERT_EQ(sink.events().size(), 1u);
    EXPECT_EQ(sink.events()[0], "wifi_ap_fallback|reason=0");
}

TEST(WifiTransitionObserverTest, EnteringApAuthFailFromConnectingEmitsApFallbackWithReason) {
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING));

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_AP_MODE_AUTH_FAIL),
                     "", 202);

    ASSERT_EQ(sink.events().size(), 1u);
    EXPECT_EQ(sink.events()[0], "wifi_ap_fallback|reason=202");
}

TEST(WifiTransitionObserverTest, ConnectedDirectlyToApEmitsDropThenFallback) {
    // A live connection that escalates straight to AP reports BOTH facts: the
    // drop out of WIFI_CONNECTED and the AP fallback (with its own reason).
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTED));
    observer.recordDisconnectReason(202);

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_AP_MODE_AUTH_FAIL),
                     "", 202);

    ASSERT_EQ(sink.events().size(), 2u);
    EXPECT_EQ(sink.events()[0], "wifi_drop|reason=202");
    EXPECT_EQ(sink.events()[1], "wifi_ap_fallback|reason=202");
}

TEST(WifiTransitionObserverTest, TransitionBetweenTheTwoApStatesEmitsNothing) {
    // Both AP states are stable AP operation; moving between them is not a
    // fresh fallback (and cannot happen via the state machine anyway).
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_AP_MODE_DEFAULT));

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_AP_MODE_AUTH_FAIL),
                     "", 0);

    EXPECT_TRUE(sink.events().empty());
}

TEST(WifiTransitionObserverTest, SameStateEmitsNothing) {
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING));

    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING),
                     "192.168.1.50", 0);

    EXPECT_TRUE(sink.events().empty());
}

TEST(WifiTransitionObserverTest, SuccessfulConnectClearsRecordedDropReason) {
    RecordingSink sink;
    WifiTransitionObserver observer(sink);
    observer.setInitialState(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTED));
    observer.recordDisconnectReason(200);

    // Drop then reconnect: the drop reports the reason; the reconnect clears it.
    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING), "", 0);
    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTED),
                     "192.168.1.50", 0);
    sink.clear();

    // A subsequent drop from the fresh connection must report reason=0 (cleared),
    // not the stale 200 from before the reconnect.
    observer.observe(static_cast<int>(esp32_firmware::WiFiState::State::WIFI_CONNECTING), "", 0);

    ASSERT_EQ(sink.events().size(), 1u);
    EXPECT_EQ(sink.events()[0], "wifi_drop|reason=0");
}

} // namespace
} // namespace
