#pragma once

// FirmwareApp.h - Thin-veneer orchestration layer for ESP32 firmware
// Extracted from can-bridge.ino for host testability
//
// Post-S1448-refactor: delegates LED pattern, discovery, and TCP server
// restart to LedPatternPolicy, DiscoveryPolicy, and TcpServerRestartPolicy.
// TokenStore extracted as a separate class (TCP auth token NVS persistence).
// Post-SRP-split: ObservabilityHub and NtpSupervisor extracted to cut
// method/field count below S1448/S1820 thresholds.

#include <cstdint>
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
#include "TokenStore.h"
#include "LedPatternPolicy.h"
#include "DiscoveryPolicy.h"
#include "TcpServerRestartPolicy.h"
#include "ObservabilityHub.h"
#include "NtpSupervisor.h"

namespace esp32_firmware {

class CanBridge;
struct CanBridgeDeps;
class AtCommandDispatcher;

// FirmwareApp - Main application orchestrator (post-SRP-split: 26 public methods)
// Delegates to: TokenStore, LedPatternPolicy, DiscoveryPolicy, TcpServerRestartPolicy,
//               ObservabilityHub, NtpSupervisor
class FirmwareApp : public ITcpHostCallbacks,
                    public IMonitorState,
                    public ICredentialClear {
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

    // Lifecycle
    void init();
    void update(uint32_t now);
    void onWiFiDisconnected(int reason);

    // Token store access (used by AT adapters)
    TokenStore& tokenStore() { return tokenStore_; }
    const TokenStore& tokenStore() const { return tokenStore_; }

    // Observability - retained for test injection; body delegates to ObservabilityHub
    void setEventLogger(IEventLogger& logger);

    // ITcpHostCallbacks
    int getWiFiState() const override;
    void onClientConnected(const std::string& ip) override;
    void onAuthFailed(const std::string& ip) override;
    void onClientDisconnected(const std::string& ip, int reason) override;
    void setMonitorActive(bool active) override;
    void handleTcpAtCommand(const std::string& cmd) override;
    void resetDiscoveryBackoff() override;

    // IMonitorState
    std::string getClientIp() const { return observabilityHub_.clientIp(); }
    bool isMonitorActive() const;

    // ICredentialClear
    void clear() override;

    // Accessors for underlying components (callers use these directly instead of
    // FirmwareApp forwarding wrappers to reduce method count).
    IWiFi& wifi() { return wifi_; }
    WiFiManager& wifiManager() {
        assert(wifiManager_ && "FirmwareApp::wifiManager() called before init()");
        return *wifiManager_;
    }
    LedPatternPolicy& ledPatternPolicy() {
        assert(ledPatternPolicy_ && "FirmwareApp::ledPatternPolicy() called before init()");
        return *ledPatternPolicy_;
    }
    DiscoveryPolicy& discoveryPolicy() {
        assert(discoveryPolicy_ && "FirmwareApp::discoveryPolicy() called before init()");
        return *discoveryPolicy_;
    }
    TcpServerRestartPolicy& tcpRestartPolicy() {
        assert(tcpRestartPolicy_ && "FirmwareApp::tcpRestartPolicy() called before init()");
        return *tcpRestartPolicy_;
    }
    CanBridge& canBridge() {
        assert(canBridge_ && "FirmwareApp::canBridge() called before init()");
        return *canBridge_;
    }
    AtCommandDispatcher& atDispatcher() {
        assert(atDispatcher_ && "FirmwareApp::atDispatcher() called before setAtCommandAdapters()");
        return *atDispatcher_;
    }

    // Configuration
    void setDiscoveryEnabled(bool enabled);
    void setAtCommandAdapters(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp,
                              IWifiCredentialStore& wifiStore, IWifiTokenStore& tokenStore,
                              IWifiCredentialClear& credClear, IMonitorState& monitor,
                              const std::array<uint8_t, 16>& deviceId);
    void setClientConnectionSource(IClientConnectionSource* source);

private:
    IWiFi& wifi_;
    IStatusLED& statusLed_;
    CanBridgeDeps canBridgeDeps_;
    IClientConnectionSource* clientConnectionSource_ = nullptr;
    const char* bakedSsid_;
    const char* bakedPass_;

    std::unique_ptr<WiFiManager> wifiManager_;
    std::unique_ptr<CanBridge> canBridge_;
    std::unique_ptr<AtCommandDispatcher> atDispatcher_;
    TokenStore tokenStore_;
    std::unique_ptr<LedPatternPolicy> ledPatternPolicy_;
    std::unique_ptr<DiscoveryPolicy> discoveryPolicy_;
    std::unique_ptr<TcpServerRestartPolicy> tcpRestartPolicy_;

    bool initialized_;
    uint32_t lastBroadcastEventIntervalMs_ = 0;

    ObservabilityHub observabilityHub_;
    std::unique_ptr<NtpSupervisor> ntpSupervisor_;
};

} // namespace esp32_firmware
