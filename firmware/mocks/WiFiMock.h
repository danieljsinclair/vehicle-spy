#pragma once

// WiFiMock.h - Mock WiFi for host testing
// Implements IWiFi and IWiFiDiscovery interfaces

#include <string>
#include <cstdint>
#include <functional>
#include <map>
#include "../vanilla/WiFiManager.h"
#include "../vanilla/DiscoveryManager.h"

namespace esp32_firmware {

class WiFiMock : public IWiFi, public IWiFiDiscovery {
public:
    enum class Mode {
        WIFI_OFF = 0,
        WIFI_STA = 1,
        WIFI_AP = 2,
        WIFI_AP_STA = 3
    };

    enum class Status {
        WL_IDLE_STATUS = 0,
        WL_NO_SSID_AVAIL = 1,
        WL_SCAN_COMPLETED = 2,
        WL_CONNECTED = 3,
        WL_CONNECT_FAILED = 4,
        WL_CONNECTION_LOST = 5,
        WL_DISCONNECTED = 6
    };

    WiFiMock() = default;

    // IWiFi interface
    void setMode(int mode) override {
        mode_ = static_cast<Mode>(mode);
    }

    void begin(const char* ssid, const char* pass) override {
        currentSsid_ = ssid ? ssid : "";
        currentPass_ = pass ? pass : "";
        if (!currentSsid_.empty()) {
            status_ = Status::WL_IDLE_STATUS;
            connectStartTimeMs_ = currentMillis_;
        }
    }

    void disconnect(bool wifiOff, bool eraseAP) override {
        (void)wifiOff;
        (void)eraseAP;
        status_ = Status::WL_DISCONNECTED;
        currentSsid_.clear();
        currentPass_.clear();
    }

    int status() const override {
        return static_cast<int>(status_);
    }

    std::string localIP() const override {
        if (mode_ == Mode::WIFI_STA && status_ == Status::WL_CONNECTED) {
            return "192.168.1.100";
        }
        return "0.0.0.0";
    }

    std::string softAPIP() const override {
        if (mode_ == Mode::WIFI_AP) {
            return "192.168.4.1";
        }
        return "0.0.0.0";
    }

    void softAP(const char* ssid, const char* pass) override {
        apSsid_ = ssid ? ssid : "";
        apPass_ = pass ? pass : "";
        mode_ = Mode::WIFI_AP;
        status_ = Status::WL_CONNECTED;
    }

    void setHostname(const char* name) override {
        hostname_ = name ? name : "";
    }

    int getMode() const override {
        return static_cast<int>(mode_);
    }

    std::string SSID() const override {
        return currentSsid_;
    }

    // IWiFiDiscovery interface (for DiscoveryManager)
    std::string broadcastIP() const override {
        if (mode_ == Mode::WIFI_STA) {
            return "192.168.1.255";
        } else if (mode_ == Mode::WIFI_AP) {
            return "192.168.4.255";
        }
        return "255.255.255.255";
    }

    const char* disconnectReasonName(int reason) const override {
        // Positional by ESP-IDF wifi_err_reason_t value: index 0 is unused
        // (real codes start at 1 = UNSPECIFIED), index 200+ continues the
        // beacon/auth-fail family.
        static const char* reasons[] = {
            "(unused-0)",
            "UNSPECIFIED",
            "AUTH_EXPIRE",
            "AUTH_LEAVE",
            "ASSOC_EXPIRE",
            "ASSOC_TOOMANY",
            "NOT_AUTHED",
            "NOT_ASSOCED",
            "ASSOC_LEAVE",
            "ASSOC_NOT_AUTHED",
            "DISASSOC_PWRCAP_BAD",
            "DISASSOC_SUPCHAN_BAD",
            "BSS_TRANSITION_DISASSOC",
            "IE_INVALID",
            "MIC_FAILURE",
            "4WAY_HANDSHAKE_TIMEOUT",
            "GROUP_KEY_UPDATE_TIMEOUT",
            "IE_IN_4WAY_DIFFERS",
            "GROUP_CIPHER_INVALID",
            "PAIRWISE_CIPHER_INVALID",
            "AKMP_INVALID",
            "UNSUPP_RSN_IE_VERSION",
            "INVALID_RSN_IE_CAP",
            "802_1X_AUTH_FAILED",
            "CIPHER_SUITE_REJECTED",
            "BEACON_TIMEOUT",
            "NO_AP_FOUND",
            "AUTH_FAIL",
            "ASSOC_FAIL",
            "HANDSHAKE_TIMEOUT"
        };
        if (reason >= 0 && reason < static_cast<int>(sizeof(reasons)/sizeof(reasons[0]))) {
            return reasons[reason];
        }
        return "UNKNOWN";
    }

    void onEvent(std::function<void(int, WifiEventInfo*)> cb, int event) override {
        eventCallbacks_[event] = std::move(cb);
    }

    // Test helpers
    void simulateConnectSuccess() {
        status_ = Status::WL_CONNECTED;
        auto it = eventCallbacks_.find(1);
        if (it != eventCallbacks_.end()) {
            it->second(1, nullptr);
        }
    }

    void simulateDisconnect(int reason) {
        status_ = Status::WL_DISCONNECTED;
        auto it = eventCallbacks_.find(2);
        if (it != eventCallbacks_.end()) {
            // Concrete event-info shape mirroring WiFiEventInfo_t. Typed through
            // the opaque WifiEventInfo* at the interface boundary (type erasure,
            // same role the old void* played, now with a meaningful name).
            struct MockEventInfo {
                struct { int reason; } wifi_sta_disconnected;
            };
            MockEventInfo info;
            info.wifi_sta_disconnected.reason = reason;
            it->second(2, reinterpret_cast<WifiEventInfo*>(&info));
        }
    }

    void setStatus(Status s) { status_ = s; }
    Mode getModeEnum() const { return mode_; }
    const std::string& getCurrentSsid() const { return currentSsid_; }
    const std::string& getCurrentPass() const { return currentPass_; }
    const std::string& getApSsid() const { return apSsid_; }
    const std::string& getHostname() const { return hostname_; }

    void reset() {
        mode_ = Mode::WIFI_OFF;
        status_ = Status::WL_DISCONNECTED;
        currentSsid_.clear();
        currentPass_.clear();
        apSsid_.clear();
        apPass_.clear();
        hostname_.clear();
        eventCallbacks_.clear();
        connectStartTimeMs_ = 0;
        currentMillis_ = 0;
    }

private:
    Mode mode_ = Mode::WIFI_OFF;
    Status status_ = Status::WL_DISCONNECTED;
    std::string currentSsid_;
    std::string currentPass_;
    std::string apSsid_;
    std::string apPass_;
    std::string hostname_;
    std::map<int, std::function<void(int, WifiEventInfo*)>> eventCallbacks_;
    uint32_t connectStartTimeMs_ = 0;
    uint32_t currentMillis_ = 0;
};

} // namespace esp32_firmware