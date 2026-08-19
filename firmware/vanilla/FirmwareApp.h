#pragma once

// FirmwareApp.h - Thin-veneer orchestration layer for ESP32 firmware
// Extracted from can-bridge.ino for host testability
//
// Post-S1448-refactor: delegates LED pattern, discovery, and TCP server
// restart to LedPatternPolicy, DiscoveryPolicy, and TcpServerRestartPolicy.
// TokenStore extracted as a separate class (TCP auth token NVS persistence).

#include <cstdint>
#include <functional>
#include <memory>
#include <array>
#include <cassert>
#include "WiFiManager.h"
#include "DiscoveryManager.h"
#include "NtpTimeSync.h"
#include "CanBridge.h"
#include "AtCommandDispatcher.h"
#include "ITcpServer.h"
#include "IClientConnectionSource.h"
#include "FactoryResetCheck.h"
#include "ISerialEventLogger.h"
#include "WifiTransitionObserver.h"
#include "TokenStore.h"
#include "LedPatternPolicy.h"
#include "DiscoveryPolicy.h"
#include "TcpServerRestartPolicy.h"

namespace esp32_firmware {

class CanBridge;
struct CanBridgeDeps;
class AtCommandDispatcher;

struct FirmwareCallbacks {
    std::function<void()> restartTcpServer;
    std::function<void()> broadcastDiscovery;
};

// FirmwareApp - Main application orchestrator (post-S1448: 35 methods)
// Delegates to: TokenStore, LedPatternPolicy, DiscoveryPolicy, TcpServerRestartPolicy
class FirmwareApp : public ITcpHostCallbacks,
                    public IMonitorState,
                    public ICredentialClear,
                    public ITransitionEventSink {
public:
    FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed, ISerial& serial,
                IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                ISntp& sntp, ITimeNtp& timeNtp,
                const std::array<uint8_t, 16>& deviceId,
                const CanBridgeDeps& canBridgeDeps,
                IClientConnectionSource* clientConnectionSource,
                const char* bakedSsid = nullptr, const char* bakedPass = nullptr,
                const char* bakedToken = "vehicle-sim-2026");

    ~FirmwareApp();

    void init();
    void update(uint32_t now);
    void onWiFiDisconnected(int reason);
    bool factoryReset();
    bool storeCredentials(const std::string& ssid, const std::string& pass);
    bool hasStoredCredentials() const;
    bool storeAuthToken(const std::string& token);

    TokenStore& tokenStore() { return tokenStore_; }
    const TokenStore& tokenStore() const { return tokenStore_; }

    void setCallbacks(const FirmwareCallbacks& callbacks);
    void setEventLogger(IEventLogger& logger);
    int getWiFiState() const override;
    void onClientConnected(const std::string& ip) override;
    void onAuthFailed(const std::string& ip) override;
    void onClientDisconnected(const std::string& ip, int reason) override;
    std::string getClientIp() const { return observability_.clientIp; }
    std::string getOwnIp() const {
        return (wifi_.getMode() == 2) ? wifi_.softAPIP() : wifi_.localIP();
    }
    struct WiFiDiagnostic {
        std::string targetSsid;
        std::string authCampaignDetail;
    };
    WiFiDiagnostic getWiFiDiagnostic() const {
        assert(wifiManager_ && "FirmwareApp::getWiFiDiagnostic called before init()");
        return WiFiDiagnostic{wifiManager_->resolveTargetSsid(),
                              wifiManager_->getAuthCampaignDetail()};
    }
    std::string getDiscoveryCadence(uint32_t nowMs) const;
    int getCurrentLedPattern() const;
    bool shouldRestartTcpServer() const;
    void clearTcpServerRestartFlag();
    bool clearCredentials();
    bool loadCredentials(std::string& ssid, std::string& pass) const;
    void clear() override;
    void setMonitorActive(bool active) override;
    bool isMonitorActive() const;
    void processCanFrames(uint32_t nowMs);
    void setSerialQuietUntilMs(uint32_t ms);
    void setDiscoveryEnabled(bool enabled);
    void resetDiscoveryBackoff() override;
    void setAtCommandAdapters(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp,
                              IWifiCredentialStore& wifiStore, IWifiTokenStore& tokenStore,
                              IWifiCredentialClear& credClear, IMonitorState& monitor,
                              const std::array<uint8_t, 16>& deviceId);
    void handleTcpAtCommand(const std::string& cmd) override;
    void handleSerialAtCommand(const std::string& cmd);
    void setClientConnectionSource(IClientConnectionSource* source);

private:
    IWiFi& wifi_;
    IStatusLED& statusLed_;
    CanBridgeDeps canBridgeDeps_;
    IClientConnectionSource* clientConnectionSource_ = nullptr;
    const char* bakedSsid_;
    const char* bakedPass_;

    std::unique_ptr<WiFiManager> wifiManager_;
    std::unique_ptr<NtpTimeSync> ntpTimeSync_;
    std::unique_ptr<CanBridge> canBridge_;
    std::unique_ptr<AtCommandDispatcher> atDispatcher_;
    TokenStore tokenStore_;
    std::unique_ptr<LedPatternPolicy> ledPatternPolicy_;
    std::unique_ptr<DiscoveryPolicy> discoveryPolicy_;
    std::unique_ptr<TcpServerRestartPolicy> tcpRestartPolicy_;

    FirmwareCallbacks callbacks_;
    bool initialized_;
    bool ntpStarted_;
    uint32_t serialQuietUntilMs_ = 0;

    struct DiscoveryState {
        bool started = false;
        bool enabled = true;
        uint32_t lastBroadcastEventIntervalMs = 0;
    };
    struct ObservabilityState {
        IEventLogger* logger = nullptr;
        std::string clientIp;
        int lastLedPattern = 0;
    };
    DiscoveryState discovery_;
    ObservabilityState observability_;
    WifiTransitionObserver wifiTransitionObserver_;

    void constructManagers(IPreferences& prefs, ISerial& serial,
                           ISntp& sntp, ITimeNtp& timeNtp);
    void setupCallbacks();
    void setupPolicies(IUdp& udp, IWiFiDiscovery& wifiDiscovery, ITime& time,
                       const std::array<uint8_t, 16>& deviceId);
    void emitEvent(const char* eventType, const std::string& detail);
    void startDiscoveryIfNeeded();
    void updateWifiStateAndEmitEvents(uint32_t now);
    void maybeStartNtp();
    void onTransitionEvent(const char* eventType, const std::string& detail) override;
};

} // namespace esp32_firmware
