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
                    return StateTransition(WiFiState::State::WIFI_CONNECTING);
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
                    return StateTransition(WiFiState::State::WIFI_CONNECTING);
                }
                break;
            }
            case CredentialSource::NONE:
            default: {
                // No credentials at all - go to AP mode
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::WIFI_AP_MODE);
            }
        }

        return StateTransition(ctx.state);  // Stay WIFI_DISCONNECTED
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
            return StateTransition(WiFiState::State::WIFI_CONNECTED, true, true);
        }

        if (status == 4 || status == 1) {  // WL_CONNECT_FAILED || WL_NO_SSID_AVAIL
            CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);

            if (shouldFallbackToApMode(source, connectDuration)) {
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::WIFI_AP_MODE);
            }

            if (shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, now, ctx.lastRetryMs)) {
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
        } else if (status == 0 && !isInitialConnectTimeout(connectDuration)) {
            // WL_IDLE_STATUS — RECONNECTING merged into WIFI_CONNECTING.
            // After onDisconnected resets lastRetryMs to 0, the first tick in
            // WIFI_CONNECTING with WL_IDLE_STATUS re-arms begin() so the stack
            // re-associates. lastRetryMs is then armed to prevent rapid re-entry.
            // The !isInitialConnectTimeout guard ensures the initial-connect
            // timeout path (fallback to AP / continue for BAKED_IN) is reachable.
            if (shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, now, ctx.lastRetryMs)) {
                CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);
                std::string storedSsid;
                std::string storedPass;
                bool hasStored = (source == CredentialSource::STORED_NVS) &&
                                loadCredentialsImpl(prefs_, storedSsid, storedPass);

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
                return StateTransition(WiFiState::State::WIFI_AP_MODE);
            } else if (source == CredentialSource::NONE) {
                // No credentials at all - go to AP mode
                wifi_.setMode(2);  // WIFI_AP
                wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
                return StateTransition(WiFiState::State::WIFI_AP_MODE);
            } else {
                // BAKED_IN credentials - keep trying (they should work)
                // RECONNECTING merged into WIFI_CONNECTING; retry loop continues here
                return StateTransition(WiFiState::State::WIFI_CONNECTING);
            }
        }

        return StateTransition(ctx.state);  // Stay in CONNECTING
    }
};

struct ConnectedStaStateHandler : public IWiFiStateHandler {
    IWiFi& wifi_;

    explicit ConnectedStaStateHandler(IWiFi& wifi) : wifi_(wifi) {}

    // Spec §1: a dropped STA connection while WIFI_CONNECTED transitions to
    // WIFI_CONNECTING (tcpRestart=true, ntp=false). RECONNECTING was merged into
    // WIFI_CONNECTING, so the per-tick self-heal lands in WIFI_CONNECTING and
    // the retry loop (shouldRetryWiFi / WIFI_CONNECTING) re-arms on the next tick.
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        (void)now;
        if (wifi_.status() != 3) {  // WL_CONNECTED
            return StateTransition(WiFiState::State::WIFI_CONNECTING, true, false);
        }
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

WiFiManager::WiFiManager(IWiFi& wifi, IPreferences& prefs,
                         const char* bakedSsid, const char* bakedPass)
    : wifi_(wifi), prefs_(prefs)
    , bakedSsid_(bakedSsid), bakedPass_(bakedPass) {
    // Initialize state handlers (RECONNECTING merged into connectingHandler_)
    disconnectedHandler_ = std::make_unique<DisconnectedStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    connectingHandler_ = std::make_unique<ConnectingStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    connectedStaHandler_ = std::make_unique<ConnectedStaStateHandler>(wifi_);
    connectedApHandler_ = std::make_unique<ConnectedApStateHandler>();
}

void WiFiManager::init() {
    ctx_.state = WiFiState::State::WIFI_DISCONNECTED;
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

    // Permanent, unrecoverable STA auth failures: the SSID/PSK combination was
    // cryptographically refused, so retrying the SAME credentials is
    // guaranteed-futile. Transition straight to AP mode (do NOT re-enter the
    // WIFI_CONNECTING connect cycle). This covers only the auth failures that
    // indicate a *wrong credential* rather than a transient link flap:
    //   - AUTH_FAIL (202): bad PSK
    //   - 802_1X_AUTH_FAILED (21): enterprise auth rejected
    //   - CIPHER_SUITE_REJECTED (22): crypto negotiation impossible
    //   - 4WAY_HANDSHAKE_TIMEOUT (15): handshake never completed (bad PSK-class)
    //
    // Transient session/assoc lifecycle reasons that are RECOVERABLE by a fresh
    // reconnect — AUTH_EXPIRE(1), AUTH_LEAVE(2), NOT_AUTHED(5), NOT_ASSOCED(6),
    // ASSOC_NOT_AUTHED(8) — fall through to WIFI_CONNECTING below so the stack
    // re-associates instead of abandoning STA for AP mode.
    if (reason == WIFI_REASON_AUTH_FAIL ||
        reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        reason == WIFI_REASON_802_1X_AUTH_FAILED ||
        reason == WIFI_REASON_CIPHER_SUITE_REJECTED) {
        wifi_.disconnect(false, true);
        wifi_.setMode(2);  // WIFI_AP
        wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
        ctx_.state = WiFiState::State::WIFI_AP_MODE;
        ctx_.tcpServerNeedsRestart = false;  // Clear flag - AP mode is stable
        // LED pattern is now owned by FirmwareApp via selectLedPattern.
        // The state transition to WIFI_AP_MODE will be reflected on the next
        // FirmwareApp::update() tick (selectLedPattern returns AP_MODE for
        // WIFI_AP_MODE when no client is connected).
        return;
    }

    if (ctx_.state == WiFiState::State::WIFI_CONNECTED) {
        ctx_.state = WiFiState::State::WIFI_CONNECTING;
        ctx_.tcpServerNeedsRestart = true;
        ctx_.lastRetryMs = 0;  // Will be set on next update
    }
}

const char* WiFiManager::stateName(WiFiState::State state) {
    switch (state) {
        case WiFiState::State::WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
        case WiFiState::State::WIFI_CONNECTING: return "WIFI_CONNECTING";
        case WiFiState::State::WIFI_CONNECTED: return "WIFI_CONNECTED";
        case WiFiState::State::WIFI_AP_MODE: return "WIFI_AP_MODE";
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
    // WIFI_CONNECTING covers both first-connect and reconnect (RECONNECTING merged).
    if (state != WiFiState::State::WIFI_DISCONNECTED &&
        state != WiFiState::State::WIFI_CONNECTING) {
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

    // LED pattern is now owned by FirmwareApp via selectLedPattern(wifiState, clientConnected).
    // WiFiManager no longer drives setPattern() — this was the first of two parallel
    // per-loop LED drivers that raced on last-writer-wins. The consolidated driver
    // (LedPatternSelector) is called once per loop from FirmwareApp::update().

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
        case WiFiState::State::WIFI_DISCONNECTED: return disconnectedHandler_.get();
        case WiFiState::State::WIFI_CONNECTING: return connectingHandler_.get();
        case WiFiState::State::WIFI_CONNECTED: return connectedStaHandler_.get();
        case WiFiState::State::WIFI_AP_MODE: return connectedApHandler_.get();
        default: return disconnectedHandler_.get();
    }
}

} // namespace esp32_firmware