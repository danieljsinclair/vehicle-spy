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
    , prefs_(prefs)
    , statusLed_(statusLed)
    , canBridgeDeps_(canBridgeDeps)
    , clientConnectionSource_(clientConnectionSource)
    , bakedSsid_(bakedSsid)
    , bakedPass_(bakedPass)
    , bakedToken_(bakedToken)
    , deviceId_(deviceId)
    , tokenStore_(prefs, bakedToken ? bakedToken : "")
    , ledPatternPolicy_(nullptr)
    , discoveryPolicy_(nullptr)
    , tcpRestartPolicy_(nullptr)
    , initialized_(false)
    , ntpStarted_(false)
    , serialQuietUntilMs_(0)
    , wifiTransitionObserver_(static_cast<ITransitionEventSink&>(*this)) {
    constructManagers(prefs, serial, sntp, timeNtp);
    setupCallbacks();
    setupPolicies(udp, wifiDiscovery, time, deviceId);
}

FirmwareApp::~FirmwareApp() = default;

void FirmwareApp::init() {
    assert(!initialized_ && "FirmwareApp::init() called twice");
    wifiManager_->init();
    canBridge_->init();
    setupCallbacks();
    initialized_ = true;
    discovery_.started = false;
    ntpStarted_ = false;
    wifiTransitionObserver_.setInitialState(static_cast<int>(wifiManager_->getState()));
}

void FirmwareApp::constructManagers(IPreferences& prefs, ISerial& serial,
                                     ISntp& sntp, ITimeNtp& timeNtp) {
    wifiManager_ = std::make_unique<WiFiManager>(wifi_, prefs, serial, bakedSsid_, bakedPass_);
    constexpr int WIFI_MODE_PLACEHOLDER = 0;
    constexpr int WIFI_STATUS_PLACEHOLDER = 0;
    ntpTimeSync_ = std::make_unique<NtpTimeSync>(sntp, timeNtp, WIFI_MODE_PLACEHOLDER, WIFI_STATUS_PLACEHOLDER);
    canBridge_ = std::make_unique<CanBridge>(canBridgeDeps_);
}

void FirmwareApp::setupCallbacks() {
    wifiManager_->setTcpServerRestartCallback([this]() {
        if (callbacks_.restartTcpServer) callbacks_.restartTcpServer();
    });
    wifiManager_->setNtpInitCallback([this]() { ntpStarted_ = true; });
    wifiManager_->setDiscoveryResetCallback([this]() {
        if (discoveryPolicy_) discoveryPolicy_->resetBackoff();
    });
}

void FirmwareApp::setupPolicies(IUdp& udp, IWiFiDiscovery& wifiDiscovery, ITime& time,
                                 const std::array<uint8_t, 16>& deviceId) {
    ledPatternPolicy_ = std::make_unique<LedPatternPolicy>(
        statusLed_, *wifiManager_, callbacks_.restartTcpServer, callbacks_.broadcastDiscovery);
    tcpRestartPolicy_ = std::make_unique<TcpServerRestartPolicy>(
        *wifiManager_, callbacks_.restartTcpServer);

    // DiscoveryPolicy owns its own DiscoveryManager. The broadcast callback
    // forwards to the firmware-effect spy AND emits the tier-change [EVENT]
    // (throttled to once per cadence-interval change).
    discoveryPolicy_ = std::make_unique<DiscoveryPolicy>(
        udp, wifiDiscovery, time, deviceId,
        [this]() {
            if (callbacks_.broadcastDiscovery) callbacks_.broadcastDiscovery();
            // Emit [EVENT] discovery_broadcast only when the cadence tier changes.
            const DiscoveryContext& ctx = discoveryPolicy_->context();
            uint32_t ageMs = (ctx.lastBroadcastMs > ctx.connectTimeMs)
                                 ? (ctx.lastBroadcastMs - ctx.connectTimeMs) : 0;
            uint32_t intervalMs = DiscoveryManager::discoveryIntervalMs(ageMs);
            if (intervalMs != discovery_.lastBroadcastEventIntervalMs) {
                discovery_.lastBroadcastEventIntervalMs = intervalMs;
                std::string cadence = (intervalMs >= 1000)
                    ? std::to_string(intervalMs / 1000) + "s"
                    : std::to_string(intervalMs) + "ms";
                emitEvent("discovery_broadcast",
                          "cadence=" + cadence + " n=" + std::to_string(discoveryPolicy_->broadcastCount()));
            }
        },
        [this]() {
            if (discoveryPolicy_) discoveryPolicy_->resetBackoff();
        });
}

void FirmwareApp::update(uint32_t now) {
    assert(initialized_ && "FirmwareApp::update() called before init()");
    startDiscoveryIfNeeded();
    updateWifiStateAndEmitEvents(now);
    if (ledPatternPolicy_) {
        bool clientConnected = clientConnectionSource_ ? clientConnectionSource_->isClientConnected() : false;
        ledPatternPolicy_->update(now, clientConnected);
    }
    maybeStartNtp();
    if (discoveryPolicy_) {
        bool clientConnected = clientConnectionSource_ ? clientConnectionSource_->isClientConnected() : false;
        discoveryPolicy_->update(now, clientConnected);
    }
}

void FirmwareApp::startDiscoveryIfNeeded() {
    if (!discovery_.started && discoveryPolicy_ && discovery_.enabled) {
        discoveryPolicy_->startIfNeeded();
        discovery_.started = true;
    }
}

void FirmwareApp::updateWifiStateAndEmitEvents(uint32_t now) {
    wifiManager_->update(now);
    wifiTransitionObserver_.observe(static_cast<int>(wifiManager_->getState()),
                                    wifi_.localIP(),
                                    wifiManager_->getContext().escalatedToApReason);
}

void FirmwareApp::maybeStartNtp() {
    if (ntpStarted_ && !ntpTimeSync_->isSynced()) {
        ntpTimeSync_->startIfWiFiConnected(wifi_.getMode(), wifi_.status());
    }
}

void FirmwareApp::onTransitionEvent(const char* eventType, const std::string& detail) {
    emitEvent(eventType, detail);
}

void FirmwareApp::emitEvent(const char* eventType, const std::string& detail) {
    if (observability_.logger) observability_.logger->logEvent(eventType, detail);
}

void FirmwareApp::onWiFiDisconnected(int reason) {
    wifiTransitionObserver_.recordDisconnectReason(reason);
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
    emitEvent("wifi_drop", detail);
}

bool FirmwareApp::factoryReset() {
    assert(wifiManager_ && "FirmwareApp::factoryReset called before init()");
    return wifiManager_->factoryReset();
}

bool FirmwareApp::storeCredentials(const std::string& ssid, const std::string& pass) {
    assert(wifiManager_ && "FirmwareApp::storeCredentials called before init()");
    return wifiManager_->storeCredentials(ssid, pass);
}

bool FirmwareApp::hasStoredCredentials() const {
    assert(wifiManager_ && "FirmwareApp::hasStoredCredentials called before init()");
    return wifiManager_->hasStoredCredentials();
}

bool FirmwareApp::storeAuthToken(const std::string& token) {
    return tokenStore_.store(token);
}

void FirmwareApp::setCallbacks(const FirmwareCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void FirmwareApp::setEventLogger(IEventLogger& logger) {
    observability_.logger = &logger;
}

int FirmwareApp::getWiFiState() const {
    assert(wifiManager_ && "FirmwareApp::getWiFiState called before init()");
    return static_cast<int>(wifiManager_->getState());
}

void FirmwareApp::onClientConnected(const std::string& ip) {
    observability_.clientIp = ip;
    emitEvent("client_connected", "ip=" + ip);
}

void FirmwareApp::onAuthFailed(const std::string& ip) {
    emitEvent("auth_fail", "ip=" + ip + " reason=bad_token");
}

void FirmwareApp::onClientDisconnected(const std::string& ip, int reason) {
    observability_.clientIp.clear();
    emitEvent("client_disconnected", "ip=" + ip + " reason=" + std::to_string(reason));
}

std::string FirmwareApp::getDiscoveryCadence(uint32_t nowMs) const {
    return discoveryPolicy_ ? discoveryPolicy_->cadenceString(nowMs) : "none";
}

int FirmwareApp::getCurrentLedPattern() const {
    return ledPatternPolicy_ ? ledPatternPolicy_->currentPattern() : observability_.lastLedPattern;
}

bool FirmwareApp::shouldRestartTcpServer() const {
    return tcpRestartPolicy_ ? tcpRestartPolicy_->shouldRestart() : false;
}

void FirmwareApp::clearTcpServerRestartFlag() {
    if (tcpRestartPolicy_) tcpRestartPolicy_->clear();
}

bool FirmwareApp::clearCredentials() {
    assert(wifiManager_ && "FirmwareApp::clearCredentials called before init()");
    return wifiManager_->clearCredentials();
}

void FirmwareApp::clear() { (void)clearCredentials(); }

bool FirmwareApp::loadCredentials(std::string& ssid, std::string& pass) const {
    assert(wifiManager_ && "FirmwareApp::loadCredentials called before init()");
    return wifiManager_->loadCredentials(ssid, pass);
}

void FirmwareApp::setMonitorActive(bool active) {
    assert(canBridge_ && "FirmwareApp::setMonitorActive called before init()");
    canBridge_->setMonitorActive(active);
}

bool FirmwareApp::isMonitorActive() const {
    assert(canBridge_ && "FirmwareApp::isMonitorActive called before init()");
    return canBridge_->isMonitorActive();
}

void FirmwareApp::processCanFrames(uint32_t nowMs) {
    assert(canBridge_ && "FirmwareApp::processCanFrames called before init()");
    canBridge_->processFrames(isMonitorActive(), nowMs, serialQuietUntilMs_);
}

void FirmwareApp::setSerialQuietUntilMs(uint32_t ms) {
    serialQuietUntilMs_ = ms;
}

void FirmwareApp::setDiscoveryEnabled(bool enabled) {
    discovery_.enabled = enabled;
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

void FirmwareApp::handleSerialAtCommand(const std::string& cmd) {
    assert(atDispatcher_ && "FirmwareApp::handleSerialAtCommand called before setAtCommandAdapters()");
    atDispatcher_->handleSerialCommand(cmd);
}

void FirmwareApp::setClientConnectionSource(IClientConnectionSource* source) {
    clientConnectionSource_ = source;
}

} // namespace esp32_firmware
