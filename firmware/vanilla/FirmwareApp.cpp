#include "FirmwareApp.h"
#include "WiFiManager.h"
#include "DiscoveryManager.h"
#include "CanBridge.h"
#include "AtCommandDispatcher.h"
#include "StatusLED.h"
#include "ISerialEventLogger.h"
#include "WiFiReasonCodes.h"
#include <cassert>
#include <string>

namespace esp32_firmware {

FirmwareApp::FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed, ISerial& serial,
                         IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                         ISntp& sntp, ITimeNtp& timeNtp,
                         const std::array<uint8_t, 16>& deviceId,
                         const CanBridgeDeps& canBridgeDeps,
                         IClientConnectionSource* clientConnectionSource,
                         const char* bakedSsid, const char* bakedPass,
                         const char* bakedToken)
    : wifi_(wifi)
    , statusLed_(statusLed)
    , canBridgeDeps_(canBridgeDeps)
    , clientConnectionSource_(clientConnectionSource)
    , bakedSsid_(bakedSsid)
    , bakedPass_(bakedPass)
    , wifiManager_(std::make_unique<WiFiManager>(wifi_, prefs, serial, bakedSsid_, bakedPass_))
    , canBridge_(std::make_unique<CanBridge>(canBridgeDeps_))
    , atDispatcher_(nullptr)
    , tokenStore_(prefs, bakedToken ? bakedToken : "")
    , ledPatternPolicy_(nullptr)
    , discoveryPolicy_(nullptr)
    , tcpRestartPolicy_(nullptr)
    , initialized_(false)
    , lastBroadcastEventIntervalMs_(0)
    , observabilityHub_()
    , ntpSupervisor_(std::make_unique<NtpSupervisor>(wifi_, std::make_unique<NtpTimeSync>(sntp, timeNtp, 0, 0))) {
    // Wire callbacks (was setupCallbacks)
    wifiManager_->setTcpServerRestartCallback([]() {
        // No-op: WiFiManager sets its own tcpServerNeedsRestart flag on the
        // transition; restartTcpServerIfNeeded() in the .ino picks it up next tick.
    });
    wifiManager_->setNtpInitCallback([this]() { ntpSupervisor_->onNtpInitRequested(); });
    wifiManager_->setDiscoveryResetCallback([this]() {
        if (discoveryPolicy_) discoveryPolicy_->resetBackoff();
    });

    // Setup policies (was setupPolicies)
    ledPatternPolicy_ = std::make_unique<LedPatternPolicy>(
        statusLed_, *wifiManager_, [](){}, [](){});

    tcpRestartPolicy_ = std::make_unique<TcpServerRestartPolicy>(
        *wifiManager_, [](){});

    // DiscoveryPolicy owns its own DiscoveryManager. The broadcast callback
    // emits the tier-change [EVENT] (throttled to once per cadence-interval change).
    discoveryPolicy_ = std::make_unique<DiscoveryPolicy>(
        udp, wifiDiscovery, time, deviceId,
        [this]() {
            // Emit [EVENT] discovery_broadcast only when the cadence tier changes.
            const DiscoveryContext& ctx = discoveryPolicy_->context();
            uint32_t ageMs = (ctx.lastBroadcastMs > ctx.connectTimeMs)
                                 ? (ctx.lastBroadcastMs - ctx.connectTimeMs) : 0;
            uint32_t intervalMs = DiscoveryManager::discoveryIntervalMs(ageMs);
            if (intervalMs != lastBroadcastEventIntervalMs_) {
                lastBroadcastEventIntervalMs_ = intervalMs;
                std::string cadence = (intervalMs >= 1000)
                    ? std::to_string(intervalMs / 1000) + "s"
                    : std::to_string(intervalMs) + "ms";
                observabilityHub_.emit("discovery_broadcast",
                          "cadence=" + cadence + " n=" + std::to_string(discoveryPolicy_->broadcastCount()));
            }
        },
        [this]() {
            if (discoveryPolicy_) discoveryPolicy_->resetBackoff();
        });

    // Seed the WiFi state observer with the post-construction state.
    observabilityHub_.observeWifiState(
        static_cast<int>(wifiManager_->getState()),
        wifi_.localIP(),
        wifiManager_->getContext().escalatedToApReason);
}

FirmwareApp::~FirmwareApp() = default;

void FirmwareApp::init() {
    assert(!initialized_ && "FirmwareApp::init() called twice");
    wifiManager_->init();
    canBridge_->init();
    initialized_ = true;
    observabilityHub_.observeWifiState(
        static_cast<int>(wifiManager_->getState()),
        wifi_.localIP(),
        wifiManager_->getContext().escalatedToApReason);
}

void FirmwareApp::update(uint32_t now) {
    assert(initialized_ && "FirmwareApp::update() called before init()");
    if (discoveryPolicy_) discoveryPolicy_->startIfNeeded();
    wifiManager_->update(now);
    observabilityHub_.observeWifiState(
        static_cast<int>(wifiManager_->getState()),
        wifi_.localIP(),
        wifiManager_->getContext().escalatedToApReason);
    if (ledPatternPolicy_) {
        bool clientConnected = clientConnectionSource_ ? clientConnectionSource_->isClientConnected() : false;
        ledPatternPolicy_->update(now, clientConnected);
    }
    ntpSupervisor_->maybeStart();
    if (discoveryPolicy_) {
        bool clientConnected = clientConnectionSource_ ? clientConnectionSource_->isClientConnected() : false;
        discoveryPolicy_->update(now, clientConnected);
    }
}

void FirmwareApp::onWiFiDisconnected(int reason) {
    observabilityHub_.recordDisconnectReason(reason);
    assert(wifiManager_ && "FirmwareApp::onWiFiDisconnected called before init()");
    wifiManager_->onDisconnected(reason);
    const char* name = wifiReasonName(reason);
    const char* phase = wifiReasonPhase(reason);
    const std::string bssid = wifi_.BSSID();
    const int8_t rssi = wifi_.RSSI();
    std::string detail = "reason=" + std::to_string(reason) + " name=" + name + " phase=" + phase;
    if (!bssid.empty()) {
        detail += " bssid=" + bssid + " rssi=" + std::to_string(static_cast<int>(rssi));
    }
    observabilityHub_.emit("wifi_drop", detail);
}

int FirmwareApp::getWiFiState() const {
    assert(wifiManager_ && "FirmwareApp::getWiFiState called before init()");
    return static_cast<int>(wifiManager_->getState());
}

void FirmwareApp::onClientConnected(const std::string& ip) {
    observabilityHub_.onClientConnected(ip);
}

void FirmwareApp::onAuthFailed(const std::string& ip) {
    observabilityHub_.onAuthFailed(ip);
}

void FirmwareApp::onClientDisconnected(const std::string& ip, int reason) {
    observabilityHub_.onClientDisconnected(ip, reason);
}

void FirmwareApp::setEventLogger(IEventLogger& logger) {
    observabilityHub_.setLogger(logger);
}

void FirmwareApp::setMonitorActive(bool active) {
    assert(canBridge_ && "FirmwareApp::setMonitorActive called before init()");
    canBridge_->setMonitorActive(active);
}

bool FirmwareApp::isMonitorActive() const {
    assert(canBridge_ && "FirmwareApp::isMonitorActive called before init()");
    return canBridge_->isMonitorActive();
}

void FirmwareApp::clear() {
    assert(wifiManager_ && "FirmwareApp::clear called before init()");
    (void)wifiManager_->clearCredentials();
}

void FirmwareApp::setDiscoveryEnabled(bool enabled) {
    if (discoveryPolicy_) discoveryPolicy_->setEnabled(enabled);
}

void FirmwareApp::resetDiscoveryBackoff() {
    if (discoveryPolicy_) discoveryPolicy_->resetBackoff();
}

void FirmwareApp::setAtCommandAdapters(ITcpClientAt& tcpClient, ISerialAt& serial,
                                       IEspAt& esp, IWifiCredentialStore& wifiStore,
                                       IWifiTokenStore& tokenStore, IWifiCredentialClear& credClear,
                                       IMonitorState& monitor,
                                       const std::array<uint8_t, 16>& deviceId) {
    atDispatcher_ = std::make_unique<AtCommandDispatcher>(tcpClient, serial, esp,
                                                          wifiStore, tokenStore, credClear,
                                                          monitor, deviceId);
}

void FirmwareApp::handleTcpAtCommand(const std::string& cmd) {
    assert(atDispatcher_ && "FirmwareApp::handleTcpAtCommand called before setAtCommandAdapters()");
    atDispatcher_->handleTcpCommand(cmd);
}

void FirmwareApp::setClientConnectionSource(IClientConnectionSource* source) {
    clientConnectionSource_ = source;
}

} // namespace esp32_firmware
