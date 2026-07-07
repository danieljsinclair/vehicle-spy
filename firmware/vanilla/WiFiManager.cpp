#include "WiFiManager.h"
#include "WiFiReasonCodes.h"
#include <memory>

namespace esp32_firmware {

// WiFiStateHandler implementations

struct DisconnectedStateHandler : public IWiFiStateHandler {
    IWiFi& wifi_;
    IPreferences& prefs_;
    const char* bakedSsid_;
    const char* bakedPass_;

    DisconnectedStateHandler(IWiFi& wifi, IPreferences& prefs, const char* bakedSsid, const char* bakedPass)
        : wifi_(wifi), prefs_(prefs), bakedSsid_(bakedSsid), bakedPass_(bakedPass) {}

    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        CredentialSource source = ::esp32_firmware::determineCredentialSource(prefs_, bakedSsid_, bakedPass_);

        switch (source) {
            case CredentialSource::STORED_NVS: {
                std::string storedSsid;
                std::string storedPass;
                if (::esp32_firmware::loadCredentialsImpl(prefs_, storedSsid, storedPass)) {
                    wifi_.setMode(1);  // WIFI_STA
                    wifi_.setHostname(WiFiConfig::HOSTNAME);
                    wifi_.begin(storedSsid.c_str(), storedPass.c_str());
                    ctx.connectStartTime = now;
                    ctx.lastRetryMs = now;
                    return StateTransition(WiFiState::State::CONNECTING);
                }
                break;
            }
            case CredentialSource::BAKED_IN: {
                if (bakedSsid_ && bakedPass_) {
                    wifi_.setMode(1);  // WIFI_STA
                    wifi_.setHostname(WiFiConfig::HOSTNAME);
                    wifi_.begin(bakedSsid_, bakedPass_);
                    ctx.connectStartTime = now;
                    ctx.lastRetryMs = now;
                    return StateTransition(WiFiState::State::CONNECTING);
                }
                break;
            }
            case CredentialSource::NONE:
            default: {
                // No credentials at all - go to AP mode
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::CONNECTED_AP);
            }
        }

        return StateTransition(ctx.state);  // Stay DISCONNECTED
    }
};

struct ConnectingStateHandler : public IWiFiStateHandler {
    IWiFi& wifi_;
    IPreferences& prefs_;
    const char* bakedSsid_;
    const char* bakedPass_;

    ConnectingStateHandler(IWiFi& wifi, IPreferences& prefs, const char* bakedSsid, const char* bakedPass)
        : wifi_(wifi), prefs_(prefs), bakedSsid_(bakedSsid), bakedPass_(bakedPass) {}

    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        int status = wifi_.status();
        uint32_t connectDuration = now - ctx.connectStartTime;

        if (status == 3) {  // WL_CONNECTED
            return StateTransition(WiFiState::State::CONNECTED_STA, true, true);
        }

        if (status == 4 || status == 1) {  // WL_CONNECT_FAILED || WL_NO_SSID_AVAIL
            CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);

            if (shouldFallbackToApMode(source, connectDuration)) {
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::CONNECTED_AP);
            }

            if (shouldRetryWiFi(WiFiState::State::CONNECTING, now, ctx.lastRetryMs)) {
                std::string storedSsid;
                std::string storedPass;
                bool hasStored = (source == CredentialSource::STORED_NVS) &&
                                loadCredentialsImpl(prefs_, storedSsid, storedPass);

                wifi_.disconnect(false, true);
                if (hasStored) {
                    wifi_.begin(storedSsid.c_str(), storedPass.c_str());
                } else if (bakedSsid_ && bakedPass_) {
                    wifi_.begin(bakedSsid_, bakedPass_);
                }
                ctx.lastRetryMs = now;
            }
        } else if (isInitialConnectTimeout(connectDuration)) {
            CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);

            if (source == CredentialSource::STORED_NVS) {
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::CONNECTED_AP);
            } else if (source == CredentialSource::NONE) {
                // No credentials at all - go to AP mode
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::CONNECTED_AP);
            } else {
                // BAKED_IN credentials - keep trying (they should work)
                return StateTransition(WiFiState::State::RECONNECTING);
            }
        }

        return StateTransition(ctx.state);  // Stay in CONNECTING
    }
};

struct ReconnectingStateHandler : public IWiFiStateHandler {
    IWiFi& wifi_;
    IPreferences& prefs_;
    const char* bakedSsid_;
    const char* bakedPass_;

    ReconnectingStateHandler(IWiFi& wifi, IPreferences& prefs, const char* bakedSsid, const char* bakedPass)
        : wifi_(wifi), prefs_(prefs), bakedSsid_(bakedSsid), bakedPass_(bakedPass) {}

    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        if (wifi_.status() == 3) {  // WL_CONNECTED
            return StateTransition(WiFiState::State::CONNECTED_STA, true, true);
        }

        if (shouldRetryWiFi(WiFiState::State::RECONNECTING, now, ctx.lastRetryMs)) {
            std::string storedSsid;
            std::string storedPass;
            if (loadCredentialsImpl(prefs_, storedSsid, storedPass)) {
                wifi_.begin(storedSsid.c_str(), storedPass.c_str());
            } else if (bakedSsid_ && bakedPass_) {
                wifi_.begin(bakedSsid_, bakedPass_);
            }
            ctx.lastRetryMs = now;
        }

        return StateTransition(ctx.state);  // Stay in RECONNECTING
    }
};

struct ConnectedStaStateHandler : public IWiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        (void)now;
        // WiFi status check would go here - but needs IWiFi reference
        // For now, just stay connected
        return StateTransition(ctx.state);
    }
};

struct ConnectedApStateHandler : public IWiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        (void)now;
        (void)ctx;
        // AP mode is stable
        return StateTransition(ctx.state);
    }
};

// WiFiManager implementation

WiFiManager::WiFiManager(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed,
                         const char* bakedSsid, const char* bakedPass)
    : wifi_(wifi), prefs_(prefs), statusLed_(statusLed)
    , bakedSsid_(bakedSsid), bakedPass_(bakedPass) {
    // Initialize state handlers
    disconnectedHandler_ = std::make_unique<DisconnectedStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    connectingHandler_ = std::make_unique<ConnectingStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    reconnectingHandler_ = std::make_unique<ReconnectingStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    connectedStaHandler_ = std::make_unique<ConnectedStaStateHandler>();
    connectedApHandler_ = std::make_unique<ConnectedApStateHandler>();
}

void WiFiManager::init() {
    ctx_.state = WiFiState::State::DISCONNECTED;
    update(0);  // Initial state machine tick
}

void WiFiManager::update(uint32_t now) {
    IWiFiStateHandler* handler = getStateHandler(ctx_.state);
    StateTransition transition = handler->execute(now, ctx_);
    applyStateTransition(transition);
}

bool WiFiManager::factoryReset() {
    return clearCredentials();
}

bool WiFiManager::storeCredentials(const std::string& ssid, const std::string& pass) {
    prefs_.begin(WiFiConfig::NVS_WIFI_NAMESPACE, false);
    bool success = prefs_.putString(WiFiConfig::NVS_WIFI_SSID, ssid) > 0;
    success = success && (prefs_.putString(WiFiConfig::NVS_WIFI_PASS, pass) > 0);
    prefs_.end();
    return success;
}

bool WiFiManager::hasStoredCredentials() const {
    IPreferences& nonConstPrefs = const_cast<IPreferences&>(prefs_);
    nonConstPrefs.begin(WiFiConfig::NVS_WIFI_NAMESPACE, true);
    size_t ssidLen = nonConstPrefs.getBytesLength(WiFiConfig::NVS_WIFI_SSID);
    size_t passLen = nonConstPrefs.getBytesLength(WiFiConfig::NVS_WIFI_PASS);
    nonConstPrefs.end();
    return (ssidLen > 0 && passLen > 0);
}

bool WiFiManager::loadCredentials(std::string& ssid, std::string& pass) const {
    return loadCredentialsImpl(prefs_, ssid, pass);
}

bool WiFiManager::clearCredentials() {
    prefs_.begin(WiFiConfig::NVS_WIFI_NAMESPACE, false);
    prefs_.clear();
    prefs_.end();
    return true;
}

void WiFiManager::onDisconnected(int reason) {
    ctx_.lastDisconnectReason = reason;

    // Special handling for AUTH_FAIL - transition straight to AP mode
    if (reason == WIFI_REASON_AUTH_FAIL ||
        reason == WIFI_REASON_AUTH_EXPIRE ||
        reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
        // Transition to AP mode immediately on auth failure
        wifi_.disconnect(false, true);
        wifi_.setMode(2);  // WIFI_AP
        wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
        ctx_.state = WiFiState::State::CONNECTED_AP;
        ctx_.tcpServerNeedsRestart = false;  // Clear flag - AP mode is stable
        statusLed_.setPattern(4);  // AP_MODE
        return;
    }

    if (ctx_.state == WiFiState::State::CONNECTED_STA) {
        ctx_.state = WiFiState::State::RECONNECTING;
        ctx_.tcpServerNeedsRestart = true;
        ctx_.lastRetryMs = 0;  // Will be set on next update
    }
}

const char* WiFiManager::stateName(WiFiState::State state) {
    switch (state) {
        case WiFiState::State::DISCONNECTED: return "DISCONNECTED";
        case WiFiState::State::CONNECTING: return "CONNECTING";
        case WiFiState::State::CONNECTED_STA: return "CONNECTED_STA";
        case WiFiState::State::CONNECTED_AP: return "CONNECTED_AP";
        case WiFiState::State::RECONNECTING: return "RECONNECTING";
        default: return "UNKNOWN";
    }
}

// Testable pure functions - namespace-level for testability

CredentialSource determineCredentialSource(IPreferences& prefs, const char* bakedSsid, const char* bakedPass) {
    prefs.begin(WiFiConfig::NVS_WIFI_NAMESPACE, true);
    size_t ssidLen = prefs.getBytesLength(WiFiConfig::NVS_WIFI_SSID);
    size_t passLen = prefs.getBytesLength(WiFiConfig::NVS_WIFI_PASS);
    prefs.end();

    if (ssidLen > 0 && passLen > 0) {
        return CredentialSource::STORED_NVS;
    }
    // No stored credentials - check if baked credentials are available
    if (bakedSsid && bakedPass && bakedSsid[0] != '\0' && bakedPass[0] != '\0') {
        return CredentialSource::BAKED_IN;
    }
    return CredentialSource::NONE;
}

bool shouldFallbackToApMode(CredentialSource source, uint32_t connectDurationMs) {
    return (source == CredentialSource::STORED_NVS) &&
           (connectDurationMs > WiFiConfig::WIFI_CONNECT_TIMEOUT_MS);
}

bool isInitialConnectTimeout(uint32_t connectDurationMs) {
    return connectDurationMs > (WiFiConfig::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                                WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS);
}

bool shouldRetryWiFi(WiFiState::State state, uint32_t now, uint32_t lastRetry) {
    if (state != WiFiState::State::DISCONNECTED &&
        state != WiFiState::State::CONNECTING &&
        state != WiFiState::State::RECONNECTING) {
        return false;
    }
    return (now - lastRetry) >= WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
}

bool loadCredentialsImpl(IPreferences& prefs, std::string& ssid, std::string& pass) {
    prefs.begin(WiFiConfig::NVS_WIFI_NAMESPACE, true);
    size_t ssidLen = prefs.getBytesLength(WiFiConfig::NVS_WIFI_SSID);
    size_t passLen = prefs.getBytesLength(WiFiConfig::NVS_WIFI_PASS);

    if (ssidLen > 0 && passLen > 0) {
        ssid = prefs.getString(WiFiConfig::NVS_WIFI_SSID);
        pass = prefs.getString(WiFiConfig::NVS_WIFI_PASS);
        prefs.end();
        return true;
    }
    prefs.end();
    return false;
}

void WiFiManager::applyStateTransition(const StateTransition& transition) {
    // Treat "stay in current state" as idempotent no-op (regardless of which state)
    if (transition.nextState == ctx_.state) {
        return;  // No transition - stay sentinel
    }

    ctx_.state = transition.nextState;

    // Update LED pattern based on WiFi state
    switch (transition.nextState) {
        case WiFiState::State::DISCONNECTED:
            statusLed_.setPattern(0);  // WIFI_SEARCHING
            break;
        case WiFiState::State::CONNECTING:
            statusLed_.setPattern(0);  // WIFI_SEARCHING
            break;
        case WiFiState::State::CONNECTED_STA:
            statusLed_.setPattern(2);  // WIFI_CONNECTED
            break;
        case WiFiState::State::CONNECTED_AP:
            statusLed_.setPattern(4);  // AP_MODE
            break;
        case WiFiState::State::RECONNECTING:
            statusLed_.setPattern(0);  // WIFI_SEARCHING
            break;
    }

    if (transition.setTcpServerRestartFlag) {
        ctx_.tcpServerNeedsRestart = true;
        if (tcpServerRestartCallback_) {
            tcpServerRestartCallback_();
        }
    }

    if (transition.initNtp) {
        if (ntpInitCallback_) {
            ntpInitCallback_();
        }
    }
}

IWiFiStateHandler* WiFiManager::getStateHandler(WiFiState::State state) {
    switch (state) {
        case WiFiState::State::DISCONNECTED: return disconnectedHandler_.get();
        case WiFiState::State::CONNECTING: return connectingHandler_.get();
        case WiFiState::State::RECONNECTING: return reconnectingHandler_.get();
        case WiFiState::State::CONNECTED_STA: return connectedStaHandler_.get();
        case WiFiState::State::CONNECTED_AP: return connectedApHandler_.get();
        default: return disconnectedHandler_.get();
    }
}

} // namespace esp32_firmware