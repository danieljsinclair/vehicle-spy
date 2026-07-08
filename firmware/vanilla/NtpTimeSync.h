#pragma once

// NtpTimeSync.h - Vanilla C++ NTP time synchronization
// Extracted from can-bridge.ino for host testability

#include <cstdint>
#include <functional>
#include "WiFiManager.h"  // For IStatusLED interface

namespace esp32_firmware {

// NTP configuration
struct NtpConfig {
    static constexpr int32_t NTP_RETRY_INTERVAL_MS = 15000;
    static constexpr uint32_t NTP_SYNC_RETRY_MAX = 3;
    static constexpr const char* NTP_SERVER = "pool.ntp.org";
};

// NTP state context
struct NtpContext {
    bool synced = false;
    uint32_t lastSyncMs = 0;
    uint32_t syncAttempts = 0;
};

// SNTP interface for time synchronization
struct ISntp {
    virtual bool enabled() const = 0;
    virtual void setOperatingMode(int mode) = 0;
    virtual void setServerName(int idx, const char* server) = 0;
    virtual void setSyncMode(int mode) = 0;
    virtual void setSyncInterval(int32_t intervalMs) = 0;
    virtual void setTimeSyncNotificationCallback(std::function<void(struct timeval*)> cb) = 0;
    virtual void init() = 0;
    virtual ~ISntp() = default;
};

// Time interface
struct ITimeNtp {
    virtual void setenv(const char* name, const char* value, int overwrite) = 0;
    virtual void tzset() = 0;
    virtual time_t time(time_t* t) = 0;
    virtual void gmtime_r(const time_t* timep, struct tm* result) = 0;
    virtual size_t strftime(char* s, size_t maxsize, const char* format, const struct tm* tm) = 0;
    virtual ~ITimeNtp() = default;
};

class NtpTimeSync {
public:
    using SyncCallback = std::function<void(bool success, const char* timeStr)>;

    NtpTimeSync(ISntp& sntp, ITimeNtp& time, IStatusLED& statusLed,
                int wifiMode, int wifiStatus);

    // Initialize NTP sync - call when WiFi connects
    void init();

    // Orchestrator seam: called every loop tick once the WiFi-connected event
    // has fired. Feeds the live WiFi mode/status AND triggers init() — keeping
    // the "when/how to start NTP" knowledge inside NtpTimeSync (SRP) rather than
    // as glue in the orchestrator. No-op once synced. The orchestrator owns the
    // live IWiFi; NtpTimeSync itself stays free of IWiFi (DI, SRP).
    void startIfWiFiConnected(int wifiMode, int wifiStatus);

    // Get current timestamp with NTP sync fallback
    uint64_t getCurrentTimestamp() const;

    // Check if NTP is synced
    bool isSynced() const { return ctx_.synced; }

    // Get last sync time
    uint32_t getLastSyncMs() const { return ctx_.lastSyncMs; }

    // Get sync attempts
    uint32_t getSyncAttempts() const { return ctx_.syncAttempts; }

    // Set sync callback
    void setSyncCallback(SyncCallback cb) { syncCallback_ = std::move(cb); }

    // Testable to be called externally when SNTP fires the callback
    void onSyncComplete(struct timeval* tv);

private:
    ISntp& sntp_;
    ITimeNtp& time_;
    IStatusLED& statusLed_;
    int wifiMode_;
    int wifiStatus_;

    NtpContext ctx_;
    SyncCallback syncCallback_;

    // Testable pure function
    static bool isTimeValid(time_t t);
};

} // namespace esp32_firmware