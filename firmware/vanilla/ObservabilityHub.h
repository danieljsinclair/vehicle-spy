#pragma once

// ObservabilityHub - owns all event emission and observability state.
// Extracted from FirmwareApp to cut method/field count (S1448/S1820).

#include "ISerialEventLogger.h"
#include "WifiTransitionObserver.h"

namespace esp32_firmware {

class ObservabilityHub : public ITransitionEventSink {
public:
    void setLogger(IEventLogger& logger) { logger_ = &logger; }
    void emit(const char* eventType, const std::string& detail) {
        if (logger_) logger_->logEvent(eventType, detail);
    }
    void onClientConnected(const std::string& ip) {
        clientIp_ = ip;
        emit("client_connected", std::string("ip=") + ip);
    }
    void onAuthFailed(const std::string& ip) {
        emit("auth_fail", std::string("ip=") + ip + " reason=bad_token");
    }
    void onClientDisconnected(const std::string& ip, int reason) {
        clientIp_.clear();
        emit("client_disconnected",
             std::string("ip=") + ip + " reason=" + std::to_string(reason));
    }
    void onTransitionEvent(const char* eventType, const std::string& detail) override {
        emit(eventType, detail);
    }
    const std::string& clientIp() const { return clientIp_; }
    void observeWifiState(int state, const std::string& localIp, int escalatedToApReason) {
        wifiTransitionObserver_.observe(state, localIp, escalatedToApReason);
    }
    void recordDisconnectReason(int reason) {
        wifiTransitionObserver_.recordDisconnectReason(reason);
    }

private:
    IEventLogger* logger_ = nullptr;
    std::string clientIp_;
    WifiTransitionObserver wifiTransitionObserver_{*this};
};

} // namespace esp32_firmware
