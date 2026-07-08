#include "FirmwareApp.h"
#include "WiFiManager.h"
#include "DiscoveryManager.h"
#include "CanBridge.h"
#include "AtCommandDispatcher.h"
#include "OtaUpdateServer.h"
#include <stdexcept>

namespace esp32_firmware {

FirmwareApp::FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed,
                         IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                         ISntp& sntp, ITimeNtp& timeNtp,
                         const std::array<uint8_t, 16>& deviceId,
                         const char* bakedSsid, const char* bakedPass)
    : wifi_(wifi)
    , prefs_(prefs)
    , statusLed_(statusLed)
    , wifiDiscovery_(wifiDiscovery)
    , udp_(udp)
    , time_(time)
    , sntp_(sntp)
    , timeNtp_(timeNtp)
    , deviceId_(deviceId)
    , bakedSsid_(bakedSsid)
    , bakedPass_(bakedPass)
    , initialized_(false) {
}

FirmwareApp::~FirmwareApp() = default;

void FirmwareApp::init() {
    if (initialized_) {
        throw std::logic_error("FirmwareApp::init() called twice");
    }

    setupManagers();
    setupCallbacks();

    initialized_ = true;
    discoveryStarted_ = false;
    ntpStarted_ = false;
}

void FirmwareApp::setupManagers() {
    // Create WiFiManager (primary state machine driver)
    wifiManager_ = std::make_unique<WiFiManager>(wifi_, prefs_, statusLed_,
                                                   bakedSsid_, bakedPass_);

    // Initialize WiFi state machine. WiFi.begin() is issued here at boot - this is
    // safe and matches the original .ino ordering (WiFi.begin ran early in setup(),
    // before tcpServer.begin()). It is async and does not open a socket.
    wifiManager_->init();

    // Create DiscoveryManager (UDP broadcast discovery)
    discoveryManager_ = std::make_unique<DiscoveryManager>(udp_, wifiDiscovery_, time_, deviceId_);

    // Create NtpTimeSync (NTP time synchronization). Construction only wires the
    // injected ISntp/ITimeNtp/IStatusLED — NO hardware/socket work here. NTP init
    // (which touches SNTP/sockets) is deferred to update() after WiFi connects,
    // matching the boot-crash lesson (never touch netif at boot).
    // wifiMode/wifiStatus are passed as 0/0 now; they are read live from the WiFi
    // adapter inside init() via NtpTimeSync's ITimeNtp call path at sync time.
    ntpTimeSync_ = std::make_unique<NtpTimeSync>(sntp_, timeNtp_, statusLed_,
                                                 0, 0);

    // NOTE: discoveryManager_->init() opens the UDP socket (udp_.begin()). On ESP32
    // this MUST NOT run during boot/static-init: when FirmwareApp::init() executes
    // the WiFi netif is not yet up, so opening the socket here aborts (Guru
    // Meditation) with zero serial output - the exact boot crash this refactor
    // introduced. Defer the UDP open to the first update() tick (which runs from
    // loop(), post-Serial.begin and after WiFi.begin has had time to bring the
    // netif up). The broadcast callback is set now regardless of init timing.
    discoveryManager_->setBroadcastCallback([this](const uint8_t* packet, size_t len) {
        (void)packet;  // Suppress unused warning
        (void)len;     // Suppress unused warning
        if (callbacks_.broadcastDiscovery) {
            callbacks_.broadcastDiscovery();
        }
    });

    // CanBridge / AtCommandDispatcher / OtaUpdateServer
    // are routed into FirmwareApp in Task #2 (one manager at a time, strict TDD).
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
    if (!initialized_) {
        throw std::logic_error("FirmwareApp::update() called before init()");
    }

    // Lazily open the UDP discovery socket on the first loop tick. This defers the
    // hardware-touching udp_.begin() out of the synchronous boot path (init()), where
    // the WiFi netif is not yet up, into loop() where WiFi.begin() has taken effect.
    if (!discoveryStarted_ && discoveryManager_) {
        discoveryManager_->init();
        discoveryStarted_ = true;
    }

    // Update WiFi state machine (primary driver)
    // WiFiManager internally handles state transitions and calls setPattern() on the LED
    wifiManager_->update(now);

    // Drive NTP start from the first loop tick AFTER WiFi reports connected.
    // WiFiManager sets ntpStarted_ (via its NTP-init callback) only when it
    // transitions to a connected STA state, so NtpTimeSync::init() — which
    // touches SNTP/sockets — never runs on the boot path (boot-crash lesson).
    if (ntpStarted_ && !ntpTimeSync_->isSynced()) {
        ntpTimeSync_->setWifiState(wifi_.getMode(), wifi_.status());
        ntpTimeSync_->init();
    }

    // Update DiscoveryManager with current time and client status
    // DiscoveryManager needs to know if we have a TCP client to adjust broadcast cadence
    // For now, we pass false (no client) - this will be wired to actual client state later
    // TODO: Wire to actual TCP client state when bridging is complete
    bool haveClient = false;
    if (discoveryManager_) {
        discoveryManager_->update(now, haveClient);
    }

    // Update LED pattern animation every tick
    // StatusLED.update() drives the current pattern animation (blinking, etc.)
    // This is separate from setPattern() which changes the pattern itself
    statusLed_.update(now);
}

void FirmwareApp::onWiFiDisconnected(int reason) {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in onWiFiDisconnected()");
    }
    wifiManager_->onDisconnected(reason);
}

bool FirmwareApp::factoryReset() {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in factoryReset()");
    }
    return wifiManager_->factoryReset();
}

bool FirmwareApp::storeCredentials(const std::string& ssid, const std::string& pass) {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in storeCredentials()");
    }
    return wifiManager_->storeCredentials(ssid, pass);
}

bool FirmwareApp::hasStoredCredentials() const {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in hasStoredCredentials()");
    }
    return wifiManager_->hasStoredCredentials();
}

void FirmwareApp::setCallbacks(const FirmwareCallbacks& callbacks) {
    callbacks_ = callbacks;
    // Callbacks are now set - DiscoveryManager callback will check and invoke them
}

int FirmwareApp::getWiFiState() const {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in getWiFiState()");
    }
    return static_cast<int>(wifiManager_->getState());
}

bool FirmwareApp::shouldRestartTcpServer() const {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in shouldRestartTcpServer()");
    }
    return wifiManager_->shouldRestartTcpServer();
}

void FirmwareApp::clearTcpServerRestartFlag() {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in clearTcpServerRestartFlag()");
    }
    wifiManager_->clearTcpServerRestartFlag();
}

bool FirmwareApp::clearCredentials() {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in clearCredentials()");
    }
    return wifiManager_->clearCredentials();
}

bool FirmwareApp::loadCredentials(std::string& ssid, std::string& pass) const {
    if (!wifiManager_) {
        throw std::logic_error("WiFiManager not initialized in loadCredentials()");
    }
    return wifiManager_->loadCredentials(ssid, pass);
}

} // namespace esp32_firmware
