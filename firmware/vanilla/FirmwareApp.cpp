#include "FirmwareApp.h"
#include "WiFiManager.h"
#include "DiscoveryManager.h"
#include "CanBridge.h"
#include "AtCommandDispatcher.h"
#include "StatusLED.h"     // firmware::StatusLED::selectLedPattern
#include "ISerialEventLogger.h"
#include <cassert>
#include <string>

namespace esp32_firmware {

FirmwareApp::FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed, ISerial& serial,
                         IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                         ISntp& sntp, ITimeNtp& timeNtp,
                         const std::array<uint8_t, 16>& deviceId,
                         const CanBridgeDeps& canBridgeDeps,
                         IClientConnectionSource& clientConnectionSource,
                         const char* bakedSsid, const char* bakedPass)
    : wifi_(wifi)
    , statusLed_(statusLed)
    , canBridgeDeps_(canBridgeDeps)
    , clientConnectionSource_(clientConnectionSource)
    , bakedSsid_(bakedSsid)
    , bakedPass_(bakedPass)
    , initialized_(false)
    , previousWifiState_(static_cast<int>(WiFiState::State::WIFI_DISCONNECTED)) {
    // Construct the owned managers here (ctor scope), where the PASSED-ONLY
    // interface refs (sntp/timeNtp/udp/wifiDiscovery/time/deviceId/prefs) are still
    // in scope. Construction ONLY wires injected refs into each manager's
    // constructor — NO hardware/socket/netif work — so it is safe at static-init
    // time (FirmwareApp is a static global in the .ino, constructed before
    // setup()). The hardware-touching init() calls (WiFi.begin, UDP open, NTP,
    // TWAI) are deferred to FirmwareApp::init()/update(), which run from setup()/
    // loop() after the netif is up. This is the cpp:S1820 forward: the PASSED-ONLY
    // refs are forwarded straight into the owning manager's constructor instead of
    // being stored as FirmwareApp members.
    constructManagers(deviceId, prefs, serial, udp, wifiDiscovery, time, sntp, timeNtp);
}

// Helper: emit a [EVENT] line through the centralized logger (no-op if no logger
// is injected — safe for tests that don't care about observability).
void FirmwareApp::emitEvent(const char* eventType, const std::string& detail) {
    if (observability_.logger) {
        observability_.logger->logEvent(eventType, detail);
    }
}

FirmwareApp::~FirmwareApp() = default;

void FirmwareApp::init() {
    assert(!initialized_ && "FirmwareApp::init() called twice");

    // Deferred boot-time initialization (must NOT run at static-init / ctor time):
    // WiFi.begin(), CanBridge TWAI driver install, and the firmware-effect
    // callbacks. These touch hardware/netif, so they wait until setup() has run.
    wifiManager_->init();
    canBridge_->init();
    setupCallbacks();

    initialized_ = true;
    discovery_.started = false;
    ntpStarted_ = false;
    // Capture post-init WiFi state so the first update() transition is accurate
    // (avoids a spurious wifi_connected event if the state machine advanced
    // during init() before the first loop tick).
    previousWifiState_ = static_cast<int>(wifiManager_->getState());
}

void FirmwareApp::constructManagers(const std::array<uint8_t, 16>& deviceId,
                                     IPreferences& prefs, ISerial& serial, IUdp& udp,
                                     IWiFiDiscovery& wifiDiscovery, ITime& time,
                                     ISntp& sntp, ITimeNtp& timeNtp) {
    // Create WiFiManager (primary state machine driver). Construction only stores
    // the injected refs; the WiFi.begin() it performs lives in WiFiManager::init(),
    // which init() defers to setup()-time. Forwards the PASSED-ONLY prefs ref
    // (received by this ctor, passed straight through) — no longer stored as a
    // FirmwareApp member. The ISerial ref is forwarded the same PASSED-ONLY way:
    // WiFiManager owns the WiFi transition tracing, so FirmwareApp never stores it.
    wifiManager_ = std::make_unique<WiFiManager>(wifi_, prefs, serial,
                                                   bakedSsid_, bakedPass_);

    // Create DiscoveryManager (UDP broadcast discovery). Forwards the PASSED-ONLY
    // udp/wifiDiscovery/time/deviceId refs (received by this ctor, passed straight
    // through) — no longer stored as FirmwareApp members.
    discoveryManager_ = std::make_unique<DiscoveryManager>(udp, wifiDiscovery, time, deviceId);

    // Create NtpTimeSync (NTP time synchronization). Forwards the PASSED-ONLY
    // sntp/timeNtp refs (received by this ctor, passed straight through) — not
    // stored as members. Construction only wires ISntp/ITimeNtp/IStatusLED; NTP
    // init (touches SNTP/sockets) is deferred to update() after WiFi connects.
    // wifiMode/wifiStatus are compile-time placeholders, overwritten by
    // WiFiManager's NTP-init callback via startIfWiFiConnected() before init()
    // reads them.
    constexpr int WIFI_MODE_PLACEHOLDER = 0;
    constexpr int WIFI_STATUS_PLACEHOLDER = 0;
    ntpTimeSync_ = std::make_unique<NtpTimeSync>(sntp, timeNtp, statusLed_,
                                                 WIFI_MODE_PLACEHOLDER, WIFI_STATUS_PLACEHOLDER);

    // The broadcast callback is set now (assignment only, no hardware). The UDP
    // socket open (udp_.begin()) is deferred to the first update() tick so it
    // never runs at static-init / before the netif is up.
    discoveryManager_->setBroadcastCallback([this](const uint8_t* packet, size_t len) {
        (void)packet;  // Suppress unused warning
        (void)len;     // Suppress unused warning
        if (callbacks_.broadcastDiscovery) {
            callbacks_.broadcastDiscovery();
        }
        // Emit [EVENT] discovery_broadcast only when the cadence tier changes.
        // The per-tick [STATE] line already shows disc=<cadence>, so per-broadcast
        // events are redundant at the rapid 500ms tier. Throttling to tier-change
        // keeps the serial log informative without flooding it.
        const DiscoveryContext& ctx = discoveryManager_->getContext();
        uint32_t ageMs = (ctx.lastBroadcastMs > ctx.connectTimeMs)
                             ? (ctx.lastBroadcastMs - ctx.connectTimeMs)
                             : 0;
        uint32_t intervalMs = DiscoveryManager::discoveryIntervalMs(ageMs);
        if (intervalMs != discovery_.lastBroadcastEventIntervalMs) {
            discovery_.lastBroadcastEventIntervalMs = intervalMs;
            std::string cadence = (intervalMs >= 1000)
                ? std::to_string(intervalMs / 1000) + "s"
                : std::to_string(intervalMs) + "ms";
            emitEvent("discovery_broadcast",
                      "cadence=" + cadence + " n=" + std::to_string(discoveryManager_->broadcastCount()));
        }
    });

    // CanBridge: construction only wires the injected ICanDriver/ITcpClient/
    // ISerialCan adapters (NO hardware/socket work). The TWAI driver itself is
    // installed/started in setup() on the ESP32, so the adapter's driverInstall/
    // start are no-ops post-boot; the actual init() (TWAI install) is deferred to
    // FirmwareApp::init() below.
    canBridge_ = std::make_unique<CanBridge>(canBridgeDeps_);
}

void FirmwareApp::setupCallbacks() {
    // Wire WiFiManager callbacks to firmware effects
    wifiManager_->setTcpServerRestartCallback([this]() {
        if (callbacks_.restartTcpServer) {
            callbacks_.restartTcpServer();
        }
    });

    // NTP init is triggered by WiFiManager when it transitions to "WiFi
    // connected" (StateTransition.initNtp == true). Defer the actual NtpTimeSync
    // start into update() via ntpStarted_ so we never touch SNTP/sockets on the
    // boot path. The callback only sets the one-shot flag.
    wifiManager_->setNtpInitCallback([this]() {
        ntpStarted_ = true;
    });
}

void FirmwareApp::update(uint32_t now) {
    assert(initialized_ && "FirmwareApp::update() called before init()");

    // Lazily open the UDP discovery socket on the first loop tick. This defers the
    // hardware-touching udp_.begin() out of the synchronous boot path (init()), where
    // the WiFi netif is not yet up, into loop() where WiFi.begin() has taken effect.
    // Gated by discovery_.enabled so the build-time VEHICLE_SIM_ENABLE_DISCOVERY=0
    // toggle keeps the socket closed (no hardware/UDP work) — the .ino sets this
    // from the macro in setup().
    if (!discovery_.started && discoveryManager_ && discovery_.enabled) {
        discoveryManager_->init();
        discovery_.started = true;
    }

    // Track previous WiFi state before update() to detect transitions.
    const int prevWifiState = previousWifiState_;

    // Update WiFi state machine (primary driver)
    // WiFiManager updates its internal state machine; it no longer drives setPattern().
    wifiManager_->update(now);

    // Detect WiFi state transitions for observability.
    const int curWifiState = static_cast<int>(wifiManager_->getState());
    if (prevWifiState != curWifiState) {
        if (curWifiState == static_cast<int>(WiFiState::State::WIFI_CONNECTED)) {
            // WiFi just connected: emit event with local IP.
            std::string ip = wifi_.localIP();
            emitEvent("wifi_connected", "ip=" + ip);
            observability_.lastDisconnectReason = 0;
        } else if (prevWifiState == static_cast<int>(WiFiState::State::WIFI_CONNECTED)
                   && curWifiState != static_cast<int>(WiFiState::State::WIFI_CONNECTED)) {
            // Dropped from connected state: emit event with last known reason.
            emitEvent("wifi_drop", "reason=" + std::to_string(observability_.lastDisconnectReason));
        }
        if (curWifiState == static_cast<int>(WiFiState::State::WIFI_AP_MODE)
            && prevWifiState != static_cast<int>(WiFiState::State::WIFI_AP_MODE)) {
            // Escalated to AP mode (definitive auth failure): emit event with
            // the reason code that triggered the escalation (set by
            // WiFiManager::onDisconnected into ctx.escalatedToApReason).
            int apReason = wifiManager_->getContext().escalatedToApReason;
            emitEvent("wifi_ap_fallback", "reason=" + std::to_string(apReason));
        }
        previousWifiState_ = curWifiState;
    }

    // ── LED pattern: the SINGLE owner per loop ────────────────────────────────
    // FirmwareApp is the sole caller of setPattern() each tick. selectLedPattern
    // is a pure function of (wifiState, clientConnected) — this kills the race
    // between WiFiManager and TcpServerManager that caused last-writer-wins LED
    // behaviour. clientConnectionSource_ queries TcpServerManager::hasClient()
    // (the manager's own view of whether a client is adopted), eliminating the
    // desync where the global WiFiClient reported disconnected while the manager
    // still held a client.
    const bool clientConnected = clientConnectionSource_.isClientConnected();
    observability_.lastLedPattern = static_cast<int>(firmware::StatusLED::selectLedPattern(
        static_cast<int>(wifiManager_->getState()), clientConnected));
    statusLed_.setPattern(observability_.lastLedPattern);

    // Drive NTP start from the first loop tick AFTER WiFi reports connected.
    // WiFiManager sets ntpStarted_ (via its NTP-init callback) only when it
    // transitions to a connected STA state, so NtpTimeSync::init() — which
    // touches SNTP/sockets — never runs on the boot path (boot-crash lesson).
    // The when/how-to-start knowledge lives inside NtpTimeSync (startIfWiFiConnected).
    if (ntpStarted_ && !ntpTimeSync_->isSynced()) {
        ntpTimeSync_->startIfWiFiConnected(wifi_.getMode(), wifi_.status());
    }

    // Drive DiscoveryManager with the current time and the live TCP-client state.
    // DiscoveryManager already knows whether to broadcast (it checks haveClient and
    // the WiFi mode internally); it opens/uses the UDP socket only after
    // discoveryManager_->init() ran above. When discovery is disabled this is a no-op.
    if (discoveryManager_ && discovery_.enabled) {
        discoveryManager_->update(now, clientConnected);
    }

    // Update LED pattern animation every tick
    // StatusLED.update() drives the current pattern animation (blinking, etc.)
    // This is separate from setPattern() which changes the pattern itself
    statusLed_.update(now);
}

void FirmwareApp::setDiscoveryEnabled(bool enabled) {
    discovery_.enabled = enabled;
}

void FirmwareApp::resetDiscoveryBackoff() {
    assert(discoveryManager_ && "FirmwareApp::resetDiscoveryBackoff called before init()");
    discoveryManager_->resetBackoff();
}

void FirmwareApp::onWiFiDisconnected(int reason) {
    observability_.lastDisconnectReason = reason;
    assert(wifiManager_ && "FirmwareApp::onWiFiDisconnected called before init()");
    wifiManager_->onDisconnected(reason);
    emitEvent("wifi_drop", "reason=" + std::to_string(reason));
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

void FirmwareApp::setCallbacks(const FirmwareCallbacks& callbacks) {
    callbacks_ = callbacks;
    // Callbacks are now set - DiscoveryManager callback will check and invoke them
}

int FirmwareApp::getWiFiState() const {
    assert(wifiManager_ && "FirmwareApp::getWiFiState called before init()");
    return static_cast<int>(wifiManager_->getState());
}

bool FirmwareApp::shouldRestartTcpServer() const {
    assert(wifiManager_ && "FirmwareApp::shouldRestartTcpServer called before init()");
    return wifiManager_->shouldRestartTcpServer();
}

void FirmwareApp::clearTcpServerRestartFlag() {
    assert(wifiManager_ && "FirmwareApp::clearTcpServerRestartFlag called before init()");
    wifiManager_->clearTcpServerRestartFlag();
}

bool FirmwareApp::clearCredentials() {
    assert(wifiManager_ && "FirmwareApp::clearCredentials called before init()");
    return wifiManager_->clearCredentials();
}

void FirmwareApp::clear() {
    // ICredentialClear seam: FactoryResetCheck only needs the wipe performed;
    // whether NVS held credentials (the bool) is not used by the reset flow.
    (void)clearCredentials();
}

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
    // getWiFiState() gates on wifiManager_ via assert internally,
    // so it is safe to rely on canBridge_ being initialized whenever this runs.
    canBridge_->processFrames(isMonitorActive(), nowMs, serialQuietUntilMs_);
}

void FirmwareApp::setSerialQuietUntilMs(uint32_t ms) {
    serialQuietUntilMs_ = ms;
}

void FirmwareApp::setAtCommandAdapters(ITcpClientAt& tcpClient, ISerialAt& serial,
                                       IEspAt& esp, IWifiCredentialStore& wifiStore,
                                       IMonitorState& monitor,
                                       const std::array<uint8_t, 16>& deviceId) {
    // Own a single dispatcher over the injected boundary adapters. The canonical
    // firmware handler set is registered lazily on first handle*() call.
    atDispatcher_ = std::make_unique<AtCommandDispatcher>(tcpClient, serial, esp,
                                                          wifiStore, monitor, deviceId);
}

void FirmwareApp::handleTcpAtCommand(const std::string& cmd) {
    assert(atDispatcher_ && "FirmwareApp::handleTcpAtCommand called before setAtCommandAdapters()");
    atDispatcher_->handleTcpCommand(cmd);
}

void FirmwareApp::handleSerialAtCommand(const std::string& cmd) {
    assert(atDispatcher_ && "FirmwareApp::handleSerialAtCommand called before setAtCommandAdapters()");
    atDispatcher_->handleSerialCommand(cmd);
}

// ── Serial observability: centralized IEventLogger ────────────────────────────

void FirmwareApp::setEventLogger(IEventLogger& logger) {
    observability_.logger = &logger;
}

void FirmwareApp::onClientConnected(const std::string& ip) {
    observability_.clientIp = ip;
    emitEvent("client_connected", "ip=" + ip);
}

void FirmwareApp::onAuthFailed(const std::string& ip) {
    emitEvent("auth_fail", "ip=" + ip + " reason=bad_token");
    // Drive LED to ERROR_AUTH_FAILURE for one tick; the next update() call to
    // selectLedPattern() will revert to the normal pattern automatically.
    statusLed_.setPattern(static_cast<int>(firmware::StatusLED::Pattern::ERROR_AUTH_FAILURE));
}

void FirmwareApp::onClientDisconnected(const std::string& ip, int reason) {
    observability_.clientIp.clear();
    emitEvent("client_disconnected", "ip=" + ip + " reason=" + std::to_string(reason));
}

std::string FirmwareApp::getDiscoveryCadence(uint32_t nowMs) const {
    if (!discoveryManager_ || !discovery_.enabled || !discovery_.started) {
        return "none";
    }
    const DiscoveryContext& ctx = discoveryManager_->getContext();
    uint32_t ageMs = (nowMs > ctx.connectTimeMs) ? (nowMs - ctx.connectTimeMs) : 0;
    uint32_t intervalMs = DiscoveryManager::discoveryIntervalMs(ageMs);
    if (intervalMs >= 1000) {
        return std::to_string(intervalMs / 1000) + "s";
    }
    return std::to_string(intervalMs) + "ms";
}

} // namespace esp32_firmware
