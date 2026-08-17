#include "NtpTimeSync.h"
#include <cstring>

namespace esp32_firmware {

NtpTimeSync::NtpTimeSync(ISntp& sntp, ITimeNtp& time, int wifiMode, int wifiStatus)
    : sntp_(sntp), time_(time)
    , wifiMode_(wifiMode), wifiStatus_(wifiStatus) {}

void NtpTimeSync::startIfWiFiConnected(int wifiMode, int wifiStatus) {
    wifiMode_ = wifiMode;
    wifiStatus_ = wifiStatus;
    init();
}

void NtpTimeSync::init() {
    if (ctx_.synced) return;

    // Don't re-init if SNTP is already running (prevents boot loop)
    if (sntp_.enabled()) return;

    ctx_.syncAttempts++;
    if (ctx_.syncAttempts > NtpConfig::NTP_SYNC_RETRY_MAX) {
        // Retry budget exhausted. NTP health is SERIAL-ONLY (the sync callback
        // reports failure): a missing NTP sync does not block AUTH or streaming,
        // so it is not a state — the LED models real states only, and the LED is
        // written by FirmwareApp's single per-tick selectLedPattern call.
        if (syncCallback_) {
            syncCallback_(false, nullptr);
        }
        return;
    }

    sntp_.setOperatingMode(1);  // SNTP_OPMODE_POLL
    sntp_.setServerName(0, NtpConfig::NTP_SERVER);
    sntp_.setSyncMode(1);  // SNTP_SYNC_MODE_IMMED
    sntp_.setSyncInterval(NtpConfig::NTP_RETRY_INTERVAL_MS);
    sntp_.setTimeSyncNotificationCallback([this](struct timeval* tv) {
        onSyncComplete(tv);
    });
    sntp_.init();

    // Set timezone to UTC
    time_.setenv("TZ", "UTC", 1);
    time_.tzset();
}

uint64_t NtpTimeSync::getCurrentTimestamp() const {
    time_t now = time_.time(nullptr);
    if (!isTimeValid(now)) {
        // Fallback to uptime-based timestamp (seconds since boot)
        return 0;  // Will be handled by caller using millis()
    }
    return static_cast<uint64_t>(now);
}

void NtpTimeSync::onSyncComplete(struct timeval* tv) {
    if (!ctx_.synced) {
        ctx_.synced = true;
        ctx_.syncAttempts = 0;

        // Convert Unix timestamp to human-readable UTC time
        time_t utcTime = tv->tv_sec;
        struct tm utcInfoBuf;
        time_.gmtime_r(&utcTime, &utcInfoBuf);
        const struct tm* const utcInfo = &utcInfoBuf;

        std::array<char, 32> timeBuf{};
        time_.strftime(timeBuf.data(), timeBuf.size(), "%Y-%m-%d %H:%M:%S UTC", utcInfo);

        if (syncCallback_) {
            syncCallback_(true, timeBuf.data());
        }
    }
    // Always update last sync time for monitoring
    ctx_.lastSyncMs = 0;  // Would use millis() in real implementation
}

bool NtpTimeSync::isTimeValid(time_t t) {
    return t >= 1000000;  // If time is clearly invalid (before Sept 2001)
}

} // namespace esp32_firmware