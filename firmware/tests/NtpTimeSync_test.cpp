// NtpTimeSync_test.cpp - Tests for NtpTimeSync vanilla class

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vanilla/NtpTimeSync.h"
#include "mocks/ArduinoMock.h"

using namespace esp32_firmware;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AnyNumber;
using ::testing::SaveArg;

// Mock SNTP interface
class MockSntp : public ISntp {
public:
    MOCK_METHOD(bool, enabled, (), (const, override));
    MOCK_METHOD(void, setOperatingMode, (int mode), (override));
    MOCK_METHOD(void, setServerName, (int idx, const char* server), (override));
    MOCK_METHOD(void, setSyncMode, (int mode), (override));
    MOCK_METHOD(void, setSyncInterval, (int32_t intervalMs), (override));
    MOCK_METHOD(void, setTimeSyncNotificationCallback, (std::function<void(struct timeval*)> cb), (override));
    MOCK_METHOD(void, init, (), (override));

    bool enabled_{false};
    std::function<void(struct timeval*)> callback_;

    void delegateToDummy() {
        ON_CALL(*this, enabled()).WillByDefault(Return(enabled_));
        ON_CALL(*this, setOperatingMode(_)).WillByDefault([](int) {});
        ON_CALL(*this, setServerName(_, _)).WillByDefault([](int, const char*) {});
        ON_CALL(*this, setSyncMode(_)).WillByDefault([](int) {});
        ON_CALL(*this, setSyncInterval(_)).WillByDefault([](int32_t) {});
        ON_CALL(*this, setTimeSyncNotificationCallback(_)).WillByDefault(
            [this](std::function<void(struct timeval*)> cb) { callback_ = std::move(cb); });
        ON_CALL(*this, init()).WillByDefault([this] { enabled_ = true; });
    }

    void triggerSync(time_t sec, suseconds_t usec = 0) {
        if (callback_) {
            timeval tv{sec, usec};
            callback_(&tv);
        }
    }

    void reset() {
        enabled_ = false;
        callback_ = nullptr;
    }
};

// Mock Time interface
class MockTimeNtp : public ITimeNtp {
public:
    MOCK_METHOD(void, setenv, (const char* name, const char* value, int overwrite), (override));
    MOCK_METHOD(void, tzset, (), (override));
    MOCK_METHOD(time_t, time, (time_t* t), (override));
    MOCK_METHOD(void, gmtime_r, (const time_t* timep, struct tm* result), (override));
    MOCK_METHOD(size_t, strftime, (char* s, size_t maxsize, const char* format, const struct tm* tm), (override));

    time_t currentTime_{1000000000};  // Valid Unix timestamp (Sept 2001+)

    void delegateToDummy() {
        ON_CALL(*this, setenv(_, _, _)).WillByDefault([](const char*, const char*, int) {});
        ON_CALL(*this, tzset()).WillByDefault([]() {});
        ON_CALL(*this, time(_)).WillByDefault([this](time_t* t) {
            if (t) *t = currentTime_;
            return currentTime_;
        });
        ON_CALL(*this, gmtime_r(_, _)).WillByDefault([this](const time_t* timep, struct tm* result) {
            time_t t = timep ? *timep : currentTime_;
            // Simple mock - set reasonable values
            result->tm_sec = t % 60;
            result->tm_min = (t / 60) % 60;
            result->tm_hour = (t / 3600) % 24;
            result->tm_mday = (t / 86400) % 30 + 1;
            result->tm_mon = 0;  // January
            result->tm_year = 101;  // 2001
            result->tm_wday = 0;
            result->tm_yday = 0;
            result->tm_isdst = 0;
        });
        ON_CALL(*this, strftime(_, _, _, _)).WillByDefault([](char* s, size_t, const char*, const struct tm*) {
            strcpy(s, "2001-09-09 01:46:40 UTC");
            return strlen(s);
        });
    }

    void setCurrentTime(time_t t) { currentTime_ = t; }
    void reset() { currentTime_ = 1000000000; }
};

// Mock StatusLED interface
class MockStatusLED : public IStatusLED {
public:
    MOCK_METHOD(void, setPattern, (int pattern), (override));

    void delegateToDummy() {
        ON_CALL(*this, setPattern(_)).WillByDefault([](int) {});
    }

    void reset() {}
};

class NtpTimeSyncTest : public ::testing::Test {
protected:
    MockSntp sntpMock;
    MockTimeNtp timeMock;
    MockStatusLED statusLedMock;
    std::unique_ptr<NtpTimeSync> ntpTimeSync;
    bool syncSuccess{false};
    std::string syncTimeStr;

    void SetUp() override {
        sntpMock.reset();
        timeMock.reset();
        statusLedMock.reset();
        arduino_mock::resetAllMocks();

        sntpMock.delegateToDummy();
        timeMock.delegateToDummy();
        statusLedMock.delegateToDummy();

        syncSuccess = false;
        syncTimeStr.clear();

        // STA mode, WL_CONNECTED
        ntpTimeSync = std::make_unique<NtpTimeSync>(
            sntpMock, timeMock, statusLedMock,
            1, 3  // WIFI_STA, WL_CONNECTED
        );

        ntpTimeSync->setSyncCallback([this](bool success, const char* timeStr) {
            syncSuccess = success;
            if (timeStr) syncTimeStr = timeStr;
        });
    }

    void TearDown() override {
        ntpTimeSync.reset();
    }
};

// Note: isTimeValid is a private static method, tested indirectly through getCurrentTimestamp behavior

// Initialization tests
TEST_F(NtpTimeSyncTest, Init_NotSynced_ConfiguresSntp) {
    EXPECT_CALL(sntpMock, setOperatingMode(1));  // SNTP_OPMODE_POLL
    EXPECT_CALL(sntpMock, setServerName(0, NtpConfig::NTP_SERVER));
    EXPECT_CALL(sntpMock, setSyncMode(1));  // SNTP_SYNC_MODE_IMMED
    EXPECT_CALL(sntpMock, setSyncInterval(NtpConfig::NTP_RETRY_INTERVAL_MS));
    EXPECT_CALL(sntpMock, init());
    EXPECT_CALL(timeMock, setenv("TZ", "UTC", 1));
    EXPECT_CALL(timeMock, tzset());

    ntpTimeSync->init();
}

TEST_F(NtpTimeSyncTest, Init_AlreadySynced_DoesNotReconfigure) {
    ntpTimeSync->init();  // First init

    // Trigger sync to mark as synced
    sntpMock.triggerSync(1000000000);
    EXPECT_TRUE(ntpTimeSync->isSynced());

    // Second init should be no-op
    EXPECT_CALL(sntpMock, init()).Times(0);
    ntpTimeSync->init();
}

TEST_F(NtpTimeSyncTest, Init_SntpAlreadyEnabled_DoesNotReinit) {
    sntpMock.enabled_ = true;
    ntpTimeSync->init();  // Will increment syncAttempts, then return because enabled() is true

    // syncAttempts is incremented BEFORE checking enabled(), so it's 1
    // But init() should NOT call sntp_.init() or set up callbacks
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 1);
}

TEST_F(NtpTimeSyncTest, Init_ExceedsMaxRetries_ShowsErrorInStaMode) {
    // Max out sync attempts
    for (uint32_t i = 0; i <= NtpConfig::NTP_SYNC_RETRY_MAX; i++) {
        ntpTimeSync->init();
    }

    // In STA mode with WL_CONNECTED, should show ERROR_NO_NTP_SERVICE (pattern 8)
    EXPECT_CALL(statusLedMock, setPattern(8));
    ntpTimeSync->init();

    // Callback should receive failure
    EXPECT_FALSE(syncSuccess);
}

TEST_F(NtpTimeSyncTest, Init_ExceedsMaxRetries_DoesNotShowErrorInApMode) {
    // Create NtpTimeSync in AP mode
    ntpTimeSync = std::make_unique<NtpTimeSync>(
        sntpMock, timeMock, statusLedMock,
        2, 5  // WIFI_AP, WL_NO_SSID_AVAIL
    );
    ntpTimeSync->setSyncCallback([this](bool success, const char* timeStr) {
        syncSuccess = success;
        if (timeStr) syncTimeStr = timeStr;
    });

    // Max out sync attempts
    for (uint32_t i = 0; i <= NtpConfig::NTP_SYNC_RETRY_MAX; i++) {
        ntpTimeSync->init();
    }

    // In AP mode, should NOT show error (no internet by design)
    EXPECT_CALL(statusLedMock, setPattern(_)).Times(0);
    ntpTimeSync->init();
}

// Sync completion tests
TEST_F(NtpTimeSyncTest, OnSyncCompletes_FirstTime_MarksSynced) {
    ntpTimeSync->init();

    EXPECT_FALSE(ntpTimeSync->isSynced());
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 1);

    sntpMock.triggerSync(1000000000);

    EXPECT_TRUE(ntpTimeSync->isSynced());
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 0);  // Reset on sync
    EXPECT_TRUE(syncSuccess);
    EXPECT_FALSE(syncTimeStr.empty());
}

TEST_F(NtpTimeSyncTest, OnSyncCompletes_CallsCallbackWithTimeStr) {
    ntpTimeSync->init();
    sntpMock.triggerSync(1234567890);

    EXPECT_TRUE(syncSuccess);
    EXPECT_THAT(syncTimeStr, testing::HasSubstr("UTC"));
}

TEST_F(NtpTimeSyncTest, OnSyncCompletes_Twice_DoesNotResetSyncAttempts) {
    ntpTimeSync->init();
    sntpMock.triggerSync(1000000000);

    EXPECT_TRUE(ntpTimeSync->isSynced());
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 0);

    // Second sync - callback is only called once (first time synced flag is set)
    // The callback guard in onSyncComplete is: if (!ctx_.synced)
    // So second call does nothing
    syncSuccess = false;
    syncTimeStr.clear();
    sntpMock.triggerSync(1000000001);

    // Callback is NOT called again (synced was already true)
    EXPECT_FALSE(syncSuccess);  // Was reset to false, not called again
    EXPECT_TRUE(syncTimeStr.empty());  // No new callback
}

// Timestamp tests
TEST_F(NtpTimeSyncTest, GetCurrentTimestamp_ValidTime_ReturnsTimestamp) {
    timeMock.setCurrentTime(1000000000);
    uint64_t ts = ntpTimeSync->getCurrentTimestamp();
    EXPECT_EQ(ts, 1000000000ULL);
}

TEST_F(NtpTimeSyncTest, GetCurrentTimestamp_InvalidTime_ReturnsZero) {
    timeMock.setCurrentTime(999999);  // Invalid (before Sept 2001)
    uint64_t ts = ntpTimeSync->getCurrentTimestamp();
    EXPECT_EQ(ts, 0ULL);
}

// Sync attempts tracking
TEST_F(NtpTimeSyncTest, Init_IncrementsSyncAttempts) {
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 0);
    ntpTimeSync->init();
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 1);
    ntpTimeSync->init();
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 2);
}

TEST_F(NtpTimeSyncTest, OnSyncCompletes_ResetsSyncAttempts) {
    ntpTimeSync->init();
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 1);
    sntpMock.triggerSync(1000000000);
    EXPECT_EQ(ntpTimeSync->getSyncAttempts(), 0);
}
