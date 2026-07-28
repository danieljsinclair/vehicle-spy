// LoopHeartbeat_test.cpp - Host tests for LoopHeartbeat
// Extracted from can-bridge.ino for host testability

#include "LoopHeartbeat.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdint>
#include <string>

using esp32_firmware::LoopHeartbeat;

namespace {

// Contract: a fresh instance does NOT fire at t=0 (interval has not elapsed).
TEST(LoopHeartbeatTest, SuppressesAtZeroOnFreshInstance) {
    LoopHeartbeat hb(5000);
    EXPECT_FALSE(hb.tick(0, 0, false));
}

// Contract: tick returns false before the interval elapses.
TEST(LoopHeartbeatTest, SuppressesBeforeInterval) {
    LoopHeartbeat hb(5000);
    (void)hb.tick(0, 0, false);           // no fire — lastTickMs_ stays 0
    EXPECT_FALSE(hb.tick(4999, 0, false));
}

// Contract: tick returns true once the interval has elapsed.
TEST(LoopHeartbeatTest, FiresAfterInterval) {
    LoopHeartbeat hb(5000);
    EXPECT_TRUE(hb.tick(5000, 0, false));
    EXPECT_EQ(hb.snapshot(), "[STATE] uptime=5000ms wifi=WIFI_DISCONNECTED monitor=idle\r\n");
}

// Contract: exact boundary (now == lastTick + interval) fires.
TEST(LoopHeartbeatTest, FiresAtExactBoundary) {
    LoopHeartbeat hb(1000);
    EXPECT_TRUE(hb.tick(1000, 0, false));
    EXPECT_EQ(hb.snapshot(), "[STATE] uptime=1000ms wifi=WIFI_DISCONNECTED monitor=idle\r\n");
}

// Contract: consecutive ticks within the same interval all return false
// (lastTickMs_ was advanced by the first tick).
TEST(LoopHeartbeatTest, SuppressesConsecutiveTicksWithinInterval) {
    LoopHeartbeat hb(5000);
    EXPECT_TRUE(hb.tick(5000, 0, false));  // fires at interval boundary
    EXPECT_FALSE(hb.tick(5001, 0, false)); // 1ms later — still within interval
    EXPECT_FALSE(hb.tick(9999, 0, false)); // still within interval
}

// Contract: each WiFi state value produces its correct name in the output.
TEST(LoopHeartbeatTest, MapsAllWiFiStates) {
    LoopHeartbeat hb(1000);
    struct Case { int state; const char* expected; };
    Case cases[] = {
        {0, "WIFI_DISCONNECTED"},
        {1, "WIFI_CONNECTING"},
        {2, "WIFI_CONNECTED"},
        {3, "WIFI_AP_MODE"},
    };
    for (size_t i = 0; i < std::size(cases); ++i) {
        // Advance past the interval each iteration so lastTickMs_ keeps up.
        uint32_t nowMs = 1000 + static_cast<uint32_t>(i) * 1000;
        ASSERT_TRUE(hb.tick(nowMs, cases[i].state, false)) << "state=" << cases[i].state << " at t=" << nowMs;
        EXPECT_THAT(hb.snapshot(), testing::HasSubstr(cases[i].expected));
    }
}

// Contract: unknown state values map to "UNKNOWN" (WiFiManager::stateName
// handles out-of-range values).
TEST(LoopHeartbeatTest, MapsUnknownStateToUnknown) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 99, false));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("UNKNOWN"));
}

// Contract: monitorActive=true produces "ACTIVE" in the output.
TEST(LoopHeartbeatTest, MonitorActiveProducesActive) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 0, true));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("monitor=ACTIVE"));
}

// Contract: monitorActive=false produces "idle" in the output.
TEST(LoopHeartbeatTest, MonitorInactiveProducesIdle) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 0, false));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("monitor=idle"));
}

// Contract: the uptime value in the output matches the nowMs argument.
TEST(LoopHeartbeatTest, UptimeMatchesNowMs) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(12345, 2, true));
    EXPECT_EQ(hb.snapshot(), "[STATE] uptime=12345ms wifi=WIFI_CONNECTED monitor=ACTIVE\r\n");
}

// Contract: output always ends with \r\n.
TEST(LoopHeartbeatTest, OutputEndsWithCrLf) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 0, false));
    EXPECT_EQ(hb.snapshot(), "[STATE] uptime=1000ms wifi=WIFI_DISCONNECTED monitor=idle\r\n");
}

} // namespace
