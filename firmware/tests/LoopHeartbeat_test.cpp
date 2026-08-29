// LoopHeartbeat_test.cpp - Host tests for LoopHeartbeat
// Extracted from can-bridge.ino for host testability

#include "LoopHeartbeat.h"
#include "FirmwareVersion.h"  // FIRMWARE_BUILD_VERSION (fw= field value)

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdint>
#include <string>

using esp32_firmware::LoopHeartbeat;

namespace {

// The fw= value is generated per build (git hash + date), so full-line
// equality is asserted only up to the fw field (StartsWith) — the field
// itself is pinned by the FwField* tests below.

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
    EXPECT_THAT(hb.snapshot(), testing::StartsWith(
        "[STATE] uptime=5000ms wifi=WIFI_DISCONNECTED ssid=none ip=none client=none disc=none led=0 monitor=idle fw="));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("fw=" FIRMWARE_BUILD_VERSION));
}

// Contract: exact boundary (now == lastTick + interval) fires.
TEST(LoopHeartbeatTest, FiresAtExactBoundary) {
    LoopHeartbeat hb(1000);
    EXPECT_TRUE(hb.tick(1000, 0, false));
    EXPECT_THAT(hb.snapshot(), testing::StartsWith(
        "[STATE] uptime=1000ms wifi=WIFI_DISCONNECTED ssid=none ip=none client=none disc=none led=0 monitor=idle fw="));
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
        {3, "WIFI_AP_MODE_DEFAULT"},
        {4, "WIFI_AP_MODE_AUTH_FAIL"},
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

// Contract: the own-IP field renders the supplied value after ssid.
TEST(LoopHeartbeatTest, OwnIpRendersAfterSsid) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 2, false, "", "none", 0, "homessid", "192.168.4.1"));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("ssid=homessid ip=192.168.4.1 client=none"));
}

// Contract: an empty own-IP renders "none" (mirrors the ssid/client idiom).
TEST(LoopHeartbeatTest, EmptyOwnIpRendersNone) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 2, false));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("ssid=none ip=none client=none"));
}

// Contract: the auth branch also carries the ip= field.
TEST(LoopHeartbeatTest, AuthBranchCarriesOwnIp) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 1, false, "", "none", 0, "net", "10.0.0.5",
                        "auth fail reason=202 strategy=1/3 loop=0/3"));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("ssid=net ip=10.0.0.5"));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("auth=auth fail"));
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
    EXPECT_THAT(hb.snapshot(), testing::StartsWith(
        "[STATE] uptime=12345ms wifi=WIFI_CONNECTED ssid=none ip=none client=none disc=none led=0 monitor=ACTIVE fw="));
}

// Contract: output always ends with \r\n.
TEST(LoopHeartbeatTest, OutputEndsWithCrLf) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 0, false));
    EXPECT_THAT(hb.snapshot(), testing::EndsWith("\r\n"));
}

// ── fw= field (build-identifying firmware version) ───────────────────────────

// Contract: the line carries the exact compiled-in build version, so a
// [STATE] snapshot identifies the running build (ATI reports the same value).
TEST(LoopHeartbeatTest, FwFieldCarriesBuildVersion) {
    LoopHeartbeat hb(1000);
    ASSERT_TRUE(hb.tick(1000, 0, false));
    EXPECT_THAT(hb.snapshot(), testing::HasSubstr("fw=" FIRMWARE_BUILD_VERSION));
}

// Contract: fw= is APPENDED at the END of the line in BOTH branches — nothing
// follows it but the terminator. Field order is append-only: consumers that
// match prefixes or earlier fields ([STATE] uptime=, wifi=, the CLI's
// stripStateLines prefix match) must not break when fields are added.
TEST(LoopHeartbeatTest, FwFieldIsLastFieldInBothBranches) {
    LoopHeartbeat hb(1000);
    const std::string fw = "fw=" FIRMWARE_BUILD_VERSION;

    ASSERT_TRUE(hb.tick(1000, 0, false));  // no-auth branch
    std::string line = hb.snapshot();
    auto pos = line.find(fw);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(line.substr(pos + fw.size()), "\r\n");
    EXPECT_GT(pos, line.find("monitor=idle"));

    ASSERT_TRUE(hb.tick(5000, 1, false, "", "none", 0, "net", "10.0.0.5",
                        "auth fail reason=202 strategy=1/3 loop=0/3"));  // auth branch
    line = hb.snapshot();
    pos = line.find(fw);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(line.substr(pos + fw.size()), "\r\n");
    EXPECT_GT(pos, line.find("auth=auth fail"));
}

} // namespace
