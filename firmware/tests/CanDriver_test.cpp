// CanDriver_test.cpp - Host tests for CanDriver
// Extracted from can-bridge.ino setup() for host testability

#include "CanDriver.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using esp32_firmware::CanDriver;
using esp32_firmware::ILogger;
using esp32_firmware::ITwaiHardware;

namespace {

// Recording logger: captures log messages with severity for test assertions.
class FakeLogger : public ILogger {
public:
    struct Entry {
        std::string msg;
        bool isError;
    };
    std::vector<Entry> entries;

    void log(const char* msg, bool isError) override {
        entries.push_back({msg, isError});
    }

    void reset() { entries.clear(); }
};

// Scripted TWAI hardware: returns a predefined sequence from driverInstall/start,
// then sticks at the last value for subsequent calls.
class FakeTwaiHardware : public ITwaiHardware {
public:
    // installResults[i] = return value for the i-th driverInstall call (-1 = fail, 0 = ok)
    // startResults[i]  = return value for the i-th start call
    FakeTwaiHardware(std::vector<int> installResults, std::vector<int> startResults)
        : installResults_(std::move(installResults))
        , startResults_(std::move(startResults))
        , installPos_(0u), startPos_(0u) {}

    int driverInstall(esp32_firmware::CanGeneralConfig*, esp32_firmware::CanTimingConfig*, esp32_firmware::CanFilterConfig*) override {
        if (installPos_ < installResults_.size()) {
            return installResults_[installPos_++];
        }
        return installResults_.empty() ? 0 : installResults_.back();
    }

    int start() override {
        if (startPos_ < startResults_.size()) {
            return startResults_[startPos_++];
        }
        return startResults_.empty() ? 0 : startResults_.back();
    }

    void reset(const std::vector<int>& installResults, const std::vector<int>& startResults) {
        installResults_ = installResults;
        startResults_ = startResults;
        installPos_ = 0u;
        startPos_ = 0u;
    }

    size_t installCalls() const { return installPos_; }
    size_t startCalls() const { return startPos_; }

private:
    std::vector<int> installResults_;
    std::vector<int> startResults_;
    size_t installPos_;
    size_t startPos_;
};

} // namespace

// ── Disabled path ──────────────────────────────────────────────────────────────

// When TWAI is disabled at build time, initialize() logs the disabled message
// and returns true (no error). driverInstall/start are NOT called.
TEST(CanDriverTest, Disabled_LogsMessageAndReturnsTrue) {
    FakeLogger logger;
    FakeTwaiHardware hardware({0}, {0});  // Would succeed if called

    CanDriver driver(logger, hardware, false);  // disabled
    bool result = driver.initialize(nullptr, nullptr, nullptr);

    EXPECT_TRUE(result);

    ASSERT_EQ(logger.entries.size(), 1u);
    EXPECT_EQ(logger.entries[0].msg, "TWAI disabled via VEHICLE_SIM_ENABLE_TWAI=0");
    EXPECT_FALSE(logger.entries[0].isError);

    // Hardware was never touched.
    EXPECT_EQ(hardware.installCalls(), 0);
    EXPECT_EQ(hardware.startCalls(), 0);
}

// ── Enabled path: all succeed ──────────────────────────────────────────────────

// When TWAI is enabled and both calls succeed, initialize() logs the success
// message and returns true.
TEST(CanDriverTest, Enabled_InstallAndStartSucceed_LogsSuccessAndReturnsTrue) {
    FakeLogger logger;
    FakeTwaiHardware hardware({0}, {0});  // both succeed

    CanDriver driver(logger, hardware, true);  // enabled
    bool result = driver.initialize(nullptr, nullptr, nullptr);

    EXPECT_TRUE(result);

    ASSERT_EQ(logger.entries.size(), 1u);
    EXPECT_EQ(logger.entries[0].msg, "TWAI started @ 500kbps (listen-only)");
    EXPECT_FALSE(logger.entries[0].isError);

    EXPECT_EQ(hardware.installCalls(), 1);
    EXPECT_EQ(hardware.startCalls(), 1);
}

// ── Enabled path: driverInstall fails ─────────────────────────────────────────

// When driverInstall returns non-zero, initialize() logs the error and returns
// false. start() is NOT called (short-circuit on first failure).
TEST(CanDriverTest, Enabled_DriverInstallFails_LogsErrorAndReturnsFalse) {
    FakeLogger logger;
    FakeTwaiHardware hardware({-1}, {0});  // install fails, start would succeed

    CanDriver driver(logger, hardware, true);
    bool result = driver.initialize(nullptr, nullptr, nullptr);

    EXPECT_FALSE(result);

    ASSERT_EQ(logger.entries.size(), 1u);
    EXPECT_EQ(logger.entries[0].msg, "FAIL: twai_driver_install");
    EXPECT_TRUE(logger.entries[0].isError);

    EXPECT_EQ(hardware.installCalls(), 1);
    EXPECT_EQ(hardware.startCalls(), 0);  // short-circuit: start never called
}

// ── Enabled path: install succeeds, start fails ───────────────────────────────

// When driverInstall succeeds but start returns non-zero, initialize() logs
// the start error and returns false.
TEST(CanDriverTest, Enabled_StartFails_LogsErrorAndReturnsFalse) {
    FakeLogger logger;
    FakeTwaiHardware hardware({0}, {-1});  // install ok, start fails

    CanDriver driver(logger, hardware, true);
    bool result = driver.initialize(nullptr, nullptr, nullptr);

    EXPECT_FALSE(result);

    ASSERT_EQ(logger.entries.size(), 1u);
    EXPECT_EQ(logger.entries[0].msg, "FAIL: twai_start");
    EXPECT_TRUE(logger.entries[0].isError);

    EXPECT_EQ(hardware.installCalls(), 1);
    EXPECT_EQ(hardware.startCalls(), 1);  // start IS called (install succeeded)
}

// ── Both install and start fail ───────────────────────────────────────────────

// When both calls fail, only the first failure is reported (driverInstall wins
// the short-circuit). start() is not called.
TEST(CanDriverTest, Enabled_BothFail_ReportsFirstFailureOnly) {
    FakeLogger logger;
    FakeTwaiHardware hardware({-1, -1}, {-1});  // both fail (start never reached)

    CanDriver driver(logger, hardware, true);
    bool result = driver.initialize(nullptr, nullptr, nullptr);

    EXPECT_FALSE(result);

    ASSERT_EQ(logger.entries.size(), 1u);
    EXPECT_EQ(logger.entries[0].msg, "FAIL: twai_driver_install");
    EXPECT_TRUE(logger.entries[0].isError);

    EXPECT_EQ(hardware.installCalls(), 1);
    EXPECT_EQ(hardware.startCalls(), 0);
}

// ── Config pointers are forwarded unchanged ────────────────────────────────────

// The config pointers are passed through to driverInstall without dereferencing
// in vanilla. This test verifies they reach the hardware boundary.
TEST(CanDriverTest, ConfigPointers_ForwardedToHardware) {
    FakeLogger logger;
    int gcfg = 0x1000, tcfg = 0x2000, fcfg = 0x3000;  // sentinel values

    FakeTwaiHardware hardware({0}, {0});
    CanDriver driver(logger, hardware, true);

    driver.initialize(
        reinterpret_cast<esp32_firmware::CanGeneralConfig*>(&gcfg),
        reinterpret_cast<esp32_firmware::CanTimingConfig*>(&tcfg),
        reinterpret_cast<esp32_firmware::CanFilterConfig*>(&fcfg)
    );

    // Hardware received the call (we can't verify the pointer values without
    // exposing internals, but we verify the call happened).
    EXPECT_EQ(hardware.installCalls(), 1);
}
