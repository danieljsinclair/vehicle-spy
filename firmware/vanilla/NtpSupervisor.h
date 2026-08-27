#pragma once

// NtpSupervisor - owns NTP time sync lifecycle and start tracking.
// Extracted from FirmwareApp to cut method/field count (S1448/S1820).

#include "WiFiManager.h"   // IWiFi
#include "NtpTimeSync.h"

namespace esp32_firmware {

class NtpSupervisor {
public:
    NtpSupervisor(IWiFi& wifi, std::unique_ptr<NtpTimeSync> ntp)
        : wifi_(wifi), ntp_(std::move(ntp)) {}

    void onNtpInitRequested() { started_ = true; }
    void maybeStart() {
        if (started_ && !ntp_->isSynced()) {
            ntp_->startIfWiFiConnected(wifi_.getMode(), wifi_.status());
        }
    }
    bool isStarted() const { return started_; }

private:
    IWiFi& wifi_;
    std::unique_ptr<NtpTimeSync> ntp_;
    bool started_ = false;
};

} // namespace esp32_firmware
