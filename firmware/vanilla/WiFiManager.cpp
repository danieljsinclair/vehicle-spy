#include "WiFiManager.h"
#include "WiFiReasonCodes.h"
#include <memory>

namespace esp32_firmware {

// WiFiStateHandler implementations

// Forward declaration of the AP-mode STA recovery helper, defined below.
// Kept as a free function (not a method) so the two-phase arm/fire protocol
// is independently readable, and so the helper can be unit-tested in
// isolation from the state-handler plumbing.
static StateTransition attemptApModeStaRecovery(IWiFi& wifi, IPreferences& prefs,
                                                const char* bakedSsid, const char* bakedPass,
                                                uint32_t now, WiFiState::Context& ctx);

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
                return StateTransition(WiFiState::State::WIFI_AP_MODE_DEFAULT);
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
    // Reset discovery backoff on (re)connect so the app finds the possibly-new IP
    // fast (resilient-reconnect req-2). Optional (empty = no-op) for headless tests.
    std::function<void()> resetDiscoveryBackoff_;

    ConnectingStateHandler(IWiFi& wifi, IPreferences& prefs, const char* bakedSsid, const char* bakedPass,
                           std::function<void()> resetDiscoveryBackoff = {})
        : wifi_(wifi), prefs_(prefs), bakedSsid_(bakedSsid), bakedPass_(bakedPass),
          resetDiscoveryBackoff_(std::move(resetDiscoveryBackoff)) {}

    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        const int status = wifi_.status();
        const uint32_t connectDuration = now - ctx.connectStartTime;

        // Dispatch by status. Each branch is a self-contained SRP helper.
        if (status == 3) {  // WL_CONNECTED
            return handleConnected(now, ctx);
        }
        if (status == 4 || status == 1) {  // WL_CONNECT_FAILED || WL_NO_SSID_AVAIL
            return handleConnectFailed(now, ctx, connectDuration);
        }
        if (status == 0 && !isInitialConnectTimeout(connectDuration)) {
            // WL_IDLE_STATUS — RECONNECTING merged into WIFI_CONNECTING. The
            // !isInitialConnectTimeout guard ensures the initial-connect
            // timeout path is reachable.
            return handleIdleStatus(now, ctx);
        }
        if (isInitialConnectTimeout(connectDuration)) {
            return handleInitialConnectTimeout(now, ctx);
        }
        return StateTransition(ctx.state);  // Stay in CONNECTING
    }

private:
    // (a) Success path: WL_CONNECTED → transition to WIFI_CONNECTED. Resets
    // the auth campaign + reconnect bookkeeping, re-evaluates the
    // IP-aware tcpRestart decision, and fires the discovery-backoff reset.
    StateTransition handleConnected(uint32_t now, WiFiState::Context& ctx) {
        // IP-aware tcpRestart: always restart (hardened) so the listening socket
        // is guaranteed re-bound after a radio reset (resilient-reconnect req-3).
        uint32_t outageMs = (ctx.disconnectStartMs > 0) ? (now - ctx.disconnectStartMs) : 0;
        bool restartTcp = shouldRestartTcpServerForReconnect(wifi_.localIP(), ctx.lastConnectedIp, outageMs);
        // RESILIENT RECONNECT (req-2): a (re)connect means the IP may have
        // changed (or the radio reset) — reset discovery backoff so the app is
        // re-broadcast at the SHORT/FAST cadence instead of resuming a long
        // backoff tier. This is what lets the app re-find the ESP32 quickly.
        if (resetDiscoveryBackoff_) {
            resetDiscoveryBackoff_();
        }
        // RESILIENT AUTH (req-3): a genuine successful connect resets the
        // auth-failure campaign so a later, real drop starts fresh.
        ctx.pendingAuthFail = false;
        ctx.authFailStrategyIndex = 0;
        ctx.authFailStrategyLoop = 0;
        ctx.reconnectPending = false;
        ctx.reconnectAttempts = 0;
        return StateTransition(WiFiState::State::WIFI_CONNECTED, restartTcp, true);
    }

    // (b) Hard fail: WL_CONNECT_FAILED / WL_NO_SSID_AVAIL → escalate to AP
    // when the initial-connect budget for stored creds has expired, otherwise
    // re-begin on the retry cadence.
    StateTransition handleConnectFailed(uint32_t now, WiFiState::Context& ctx, uint32_t connectDuration) {
        CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);

        if (shouldFallbackToApMode(source, connectDuration)) {
            wifi_.setMode(2);  // WIFI_AP
            wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
            // Credentials existed and could not connect — error state, so the
            // wifi_ap_fallback event keeps reporting a reason.
            ctx.escalatedToApReason = ctx.lastDisconnectReason;
            return StateTransition(WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);
        }

        if (shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, now, ctx.lastRetryMs, ctx.reconnectAttempts)) {
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
            ++ctx.reconnectAttempts;
        }
        return StateTransition(ctx.state);
    }

    // (c) WL_IDLE_STATUS — re-arm begin() on the retry cadence. After
    // onDisconnected resets lastRetryMs to 0, the first tick in
    // WIFI_CONNECTING with WL_IDLE_STATUS re-arms begin() so the stack
    // re-associates. lastRetryMs is then armed to prevent rapid re-entry.
    StateTransition handleIdleStatus(uint32_t now, WiFiState::Context& ctx) {
        if (shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, now, ctx.lastRetryMs, ctx.reconnectAttempts)) {
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
            ++ctx.reconnectAttempts;
        }
        return StateTransition(ctx.state);
    }

    // (d) Initial-connect timeout: 5 minutes of "no progress". Escalate ONLY
    // when the last-known failure is an AUTH-MECHANISM failure (the campaign
    // should have already done this, but if no onDisconnected event fired
    // this is the safety net) OR no credentials are configured. Otherwise
    // (BAKED_IN or LINK-LEVEL drop with stored creds) keep retrying — the
    // mesh will be back.
    StateTransition handleInitialConnectTimeout(uint32_t /*now*/, WiFiState::Context& ctx) {
        CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);

        // LINK-LEVEL DROP RESILIENCE (mesh-reboot fix): the previous behaviour
        // escalated to WIFI_AP_MODE_AUTH_FAIL after 5 minutes of "no progress"
        // regardless of WHY. The field log shows reason 0/200/201 (mesh AP
        // rebooting) on this exact path — escalating to AP mode is wrong, the
        // mesh will be back in 2–30 minutes and the device should keep
        // retrying STA. Escalate ONLY when the last-known failure is an
        // AUTH-MECHANISM failure (the campaign should have already done this,
        // but if no onDisconnected event fired this is the safety net).
        if (source == CredentialSource::STORED_NVS &&
            isAuthMechanismFailure(ctx.lastDisconnectReason)) {
            wifi_.setMode(2);  // WIFI_AP
            wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
            // Credentials existed and the initial-connect budget expired
            // for an AUTH-failure reason — error state, so the
            // wifi_ap_fallback event keeps reporting a reason.
            ctx.escalatedToApReason = ctx.lastDisconnectReason;
            return StateTransition(WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);
        }
        if (source == CredentialSource::NONE) {
            // No credentials at all - go to AP mode (DEFAULT).
            wifi_.setMode(2);  // WIFI_AP
            wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
            return StateTransition(WiFiState::State::WIFI_AP_MODE_DEFAULT);
        }
        // BAKED_IN credentials — keep trying (they should work).
        // OR: STORED_NVS with a LINK-LEVEL drop (mesh rebooting) — keep
        // trying; the mesh will be back. The existing 5s retry cadence
        // is fine: the radio re-associates promptly once the mesh
        // returns, and the 5s tick is well within the 30-min mesh-reboot
        // window the field reports. A longer backoff was considered
        // (WIFI_CONNECT_LINK_LEVEL_BACKOFF_MS) but adds complexity
        // without a clear field benefit; revisit if hammering becomes
        // an actual complaint. Either way, stay in WIFI_CONNECTING.
        return StateTransition(WiFiState::State::WIFI_CONNECTING);
    }
};

struct ConnectedStaStateHandler : public IWiFiStateHandler {
    IWiFi& wifi_;

    explicit ConnectedStaStateHandler(IWiFi& wifi) : wifi_(wifi) {}

    // Spec §1: a dropped STA connection while WIFI_CONNECTED transitions to
    // WIFI_CONNECTING. The tcpRestart decision is deferred to the re-connect
    // (ConnectingStateHandler) where the new IP is known — see
    // shouldRestartTcpServerForReconnect. Here we only record that a reconnect
    // is pending and stamp the drop time for the outage-duration safety check.
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        // Invariant: WIFI_CONNECTED must have a real IP. On ESP32 the core can
        // report WL_CONNECTED (status==3) across an AP toggled off-then-on while
        // DHCP does NOT re-complete, leaving localIP()==0.0.0.0. The state then
        // claims "connected" with no address — contradictory and unreachable.
        // Treat "connected but no IP" as a drop so we re-enter CONNECTING and
        // re-acquire the address promptly instead of sitting on a stale 0.0.0.0.
        if (wifi_.status() != 3 || wifi_.localIP() == "0.0.0.0") {  // WL_CONNECTED
            ctx.reconnectPending = true;
            ctx.disconnectStartMs = now;
            return StateTransition(WiFiState::State::WIFI_CONNECTING, false, false);
        }
        return StateTransition(ctx.state);
    }
};

struct ConnectedApStateHandler : public IWiFiStateHandler {
    IWiFi& wifi_;
    IPreferences& prefs_;
    const char* bakedSsid_;
    const char* bakedPass_;

    ConnectedApStateHandler(IWiFi& wifi, IPreferences& prefs, const char* bakedSsid, const char* bakedPass)
        : wifi_(wifi), prefs_(prefs), bakedSsid_(bakedSsid), bakedPass_(bakedPass) {}

    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        // WIFI_AP_MODE_DEFAULT (no credentials ever configured) has nothing to
        // retry — keep the AP stable. Only WIFI_AP_MODE_AUTH_FAIL self-heals.
        if (ctx.state != WiFiState::State::WIFI_AP_MODE_AUTH_FAIL) {
            return StateTransition(ctx.state);
        }
        return attemptApModeStaRecovery(wifi_, prefs_, bakedSsid_, bakedPass_, now, ctx);
    }
};

// AP-MODE RECOVERY: pure-ish helper (still needs the wifi/prefs/baked refs
// because it actually re-issues begin()) extracted from ConnectedApStateHandler
// so each branch is independently testable. Returns the next state transition:
// either "stay in AP mode" (no action / timer not yet elapsed) or
// "transition to WIFI_CONNECTING" (fire the STA re-association attempt).
//
// The two-phase protocol — first tick arms the timer, second tick (past
// interval) fires the attempt — is a single, atomic, deterministic state
// machine extension; both phases live here for cohesion.
static StateTransition attemptApModeStaRecovery(IWiFi& wifi, IPreferences& prefs,
                                                const char* bakedSsid, const char* bakedPass,
                                                uint32_t now, WiFiState::Context& ctx) {
    // First entry to AUTH_FAIL: arm the retry timer.
    if (ctx.apModeStaRetryMs == 0) {
        ctx.apModeStaRetryMs = now;
        return StateTransition(ctx.state);
    }

    if (now - ctx.apModeStaRetryMs < WiFiConfig::WIFI_AP_MODE_STA_RETRY_INTERVAL_MS) {
        return StateTransition(ctx.state);
    }

    // Time to attempt a STA re-association. Re-begin with whichever
    // credentials we know about (stored NVS wins, else baked-in). The
    // connecting handler will take over from here: if it succeeds, we
    // transition to WIFI_CONNECTED; if it fails, onDisconnected() will
    // re-enter the appropriate path (auth campaign for auth reasons,
    // connecting-retry-loop for link-level drops).
    CredentialSource source = determineCredentialSource(prefs, bakedSsid, bakedPass);
    std::string storedSsid;
    std::string storedPass;
    bool hasStored = (source == CredentialSource::STORED_NVS) &&
                     loadCredentialsImpl(prefs, storedSsid, storedPass);

    wifi.setMode(1);  // WIFI_STA
    if (hasStored) {
        wifi.begin(storedSsid.c_str(), storedPass.c_str());
    } else if (bakedSsid && bakedPass) {
        wifi.begin(bakedSsid, bakedPass);
    }
    // Reset the retry timer so a subsequent failure (which re-enters AP
    // mode) doesn't immediately re-fire on the next tick.
    ctx.apModeStaRetryMs = now;
    ctx.connectStartTime = now;
    ctx.lastRetryMs = now;
    return StateTransition(WiFiState::State::WIFI_CONNECTING);
}

// ── RESILIENT AUTH pure helpers (testable without hardware) ──────────────────

bool isAuthMechanismFailure(int reason) {
    // These codes were previously treated as "definitive wrong credential"
    // (immediate AP-mode bail). The verified diagnosis shows that is wrong: at
    // this layer we CANNOT tell wrong-PSK from a wrong auth MECHANISM (WPA3/SAE
    // rejecting a WPA2 client) or a transient/radio-not-settled failure. A button
    // reset connects successfully, proving the password is correct. So these must
    // trigger the strategy-exhaustion campaign, not an instant give-up.
    return reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_802_1X_AUTH_FAILED ||
           reason == WIFI_REASON_AUTH_FAIL;
}

bool isLinkLevelDrop(int reason) {
    // LINK-LEVEL / RECOVERABLE drops: the AP is unreachable, the credentials
    // are (presumed) correct, the link just broke. These MUST NOT trigger AP-mode
    // escalation. The complementary classification to isAuthMechanismFailure() —
    // a reason is one or the other (or unknown), and the two together cover the
    // codes the field has actually produced. SRP: this function is a single
    // boolean predicate on a reason code, testable without hardware.
    //
    //   - 0                 : generic/unspecified — mesh AP gone (Deco reboot)
    //   - 2 (AUTH_EXPIRE)   : session timed out, creds still valid
    //   - 3 (AUTH_LEAVE)    : we left voluntarily / mesh disassociated us
    //   - 4 (ASSOC_EXPIRE)  : association expired, router may have rebooted
    //   - 24 (CIPHER_SUITE_REJECTED) : cipher rejected at handshake
    //   - 200 (BEACON_TIMEOUT) : mesh AP stopped sending beacons
    //   - 201 (NO_AP_FOUND)  : the AP we were looking for is gone
    //   - 204 (HANDSHAKE_TIMEOUT) : handshake didn't complete in time
    return reason == 0 ||
           reason == WIFI_REASON_AUTH_EXPIRE ||
           reason == WIFI_REASON_AUTH_LEAVE ||
           reason == WIFI_REASON_ASSOC_EXPIRE ||
           reason == WIFI_REASON_CIPHER_SUITE_REJECTED ||
           reason == WIFI_REASON_BEACON_TIMEOUT ||
           reason == WIFI_REASON_NO_AP_FOUND ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

bool isAuthCampaignExhausted(int /*strategyIndex*/, int loopIndex) {
    // Exhausted = we have completed WIFI_AUTH_STRATEGY_LOOP_COUNT full passes
    // through the strategy list. The campaign advances until the last strategy of
    // the last loop has been attempted; at that point loopIndex reaches the cap.
    return loopIndex >= static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
}

void applyAuthStrategy(IWiFi& wifi, CredentialSource source,
                       const std::string& storedSsid, const std::string& storedPass,
                       const char* bakedSsid, const char* bakedPass, int index) {
    // Resolve which credentials to use: stored NVS wins, else baked-in.
    const char* ssid = nullptr;
    const char* pass = nullptr;
    if (source == CredentialSource::STORED_NVS && !storedSsid.empty()) {
        ssid = storedSsid.c_str();
        pass = storedPass.c_str();
    } else if (bakedSsid && bakedPass && bakedSsid[0] != '\0' && bakedPass[0] != '\0') {
        ssid = bakedSsid;
        pass = bakedPass;
    }

    // Clamp to the valid strategy range (defensive only at this internal seam;
    // the caller always passes a valid rotating index).
    const int i = (index < 0) ? 0
                 : (index >= static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT))
                   ? static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT) - 1
                   : index;

    switch (static_cast<AuthStrategy>(i)) {
        case AuthStrategy::BestReassociate:
            // Best / least-disruptive: re-issue the plain association attempt.
            // (Mirrors a normal reconnect; cheap and usually sufficient once the
            // radio/PHY has settled.)
            if (ssid) {
                wifi.begin(ssid, pass);
            }
            break;
        case AuthStrategy::ResetStaMode:
            // Mid: tear down the STA session and reset to STA mode, then re-begin.
            // Resolves a stuck auth/assoc state-machine on the client side.
            wifi.disconnect(false, true);
            wifi.setMode(1);  // WIFI_STA
            if (ssid) {
                wifi.begin(ssid, pass);
            }
            break;
        case AuthStrategy::FullRadioCycle:
        default:
            // Worst / most-disruptive: full radio cycle — power the netif OFF,
            // bring it back as STA, then begin. Mirrors a manual button-reset,
            // which is what reliably recovers the field failure.
            wifi.setMode(0);  // WIFI_OFF
            wifi.setMode(1);  // WIFI_STA
            if (ssid) {
                wifi.begin(ssid, pass);
            }
            break;
    }
}

// ── AuthCampaign implementation ───────────────────────────────────────────────

AuthCampaign::AuthCampaign(IWiFi& wifi, IPreferences& prefs, ISerial& serial,
                           WiFiManager& owner, const char* bakedSsid, const char* bakedPass)
    : wifi_(wifi), prefs_(prefs), serial_(serial),
      owner_(owner), bakedSsid_(bakedSsid), bakedPass_(bakedPass) {}

std::optional<StateTransition> AuthCampaign::checkCampaignSuccess(uint32_t now) {
    // A genuine successful connect during the campaign: reset the rotation so a
    // later, real drop starts fresh, then transition to CONNECTED (tcpRestart +
    // NTP init, same as the normal handler).
    if (wifi_.status() != 3) {  // not WL_CONNECTED
        return std::nullopt;
    }
    uint32_t outageMs = (owner_.ctx_.disconnectStartMs > 0)
                      ? (now - owner_.ctx_.disconnectStartMs) : 0;
    bool restartTcp = shouldRestartTcpServerForReconnect(wifi_.localIP(),
                                                         owner_.ctx_.lastConnectedIp, outageMs);
    owner_.fireDiscoveryResetCallback();
    owner_.ctx_.pendingAuthFail = false;
    owner_.ctx_.authFailStrategyIndex = 0;
    owner_.ctx_.authFailStrategyLoop = 0;
    owner_.ctx_.reconnectPending = false;
    owner_.ctx_.reconnectAttempts = 0;
    return StateTransition(WiFiState::State::WIFI_CONNECTED, restartTcp, true);
}

void AuthCampaign::applyCurrentStrategy(uint32_t /*now*/) {
    // Apply the CURRENT strategy to the radio, then advance the rotation: the
    // strategy index wraps within the list, and on wrap we advance the loop
    // counter (so the whole list is repeated each pass).
    CredentialSource source = determineCredentialSource(prefs_, bakedSsid_, bakedPass_);
    std::string storedSsid;
    std::string storedPass;
    loadCredentialsImpl(prefs_, storedSsid, storedPass);

    applyAuthStrategy(wifi_, source, storedSsid, storedPass,
                      bakedSsid_, bakedPass_, owner_.ctx_.authFailStrategyIndex);
    serial_.printf("[STATE] WiFi: %s (auth fail reason=%d strategy=%d/%u loop=%d/%u)\r\n",
                   WiFiManager::stateName(owner_.ctx_.state), owner_.ctx_.lastDisconnectReason,
                   owner_.ctx_.authFailStrategyIndex,
                   WiFiConfig::WIFI_AUTH_STRATEGY_COUNT,
                   owner_.ctx_.authFailStrategyLoop,
                   WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
}

void AuthCampaign::advanceRotation(uint32_t now) {
    ++owner_.ctx_.authFailStrategyIndex;
    if (owner_.ctx_.authFailStrategyIndex
        >= static_cast<int>(WiFiConfig::WIFI_AUTH_STRATEGY_COUNT)) {
        owner_.ctx_.authFailStrategyIndex = 0;
        ++owner_.ctx_.authFailStrategyLoop;
    }
    owner_.ctx_.lastRetryMs = now;
    ++owner_.ctx_.reconnectAttempts;
}

std::optional<StateTransition> AuthCampaign::checkExhaustion() {
    // Bounded escalation: the final strategy of the final loop has just been
    // attempted (loop counter advanced past the last lap). Only now — every
    // connection opportunity EXHAUSTED — do we give up and fall back to AP mode.
    if (!isAuthCampaignExhausted(owner_.ctx_.authFailStrategyIndex,
                                 owner_.ctx_.authFailStrategyLoop)) {
        return std::nullopt;
    }
    wifi_.setMode(2);  // WIFI_AP
    wifi_.softAP(WiFiConfig::AP_SSID, WiFiConfig::AP_PASS);
    owner_.ctx_.pendingAuthFail = false;  // campaign over; do NOT re-enter it
    owner_.ctx_.escalatedToApReason = owner_.ctx_.lastDisconnectReason;  // record the true auth reason
    serial_.printf("[STATE] WiFi: %s -> WIFI_AP_MODE_AUTH_FAIL (auth exhausted reason=%d after %u strategies x %u loops)\r\n",
                   WiFiManager::stateName(owner_.ctx_.state), owner_.ctx_.lastDisconnectReason,
                   WiFiConfig::WIFI_AUTH_STRATEGY_COUNT,
                   WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
    return StateTransition(WiFiState::State::WIFI_AP_MODE_AUTH_FAIL);
}

StateTransition AuthCampaign::tick(uint32_t now) {
    // (1) Already connected mid-campaign → transition to CONNECTED.
    if (auto success = checkCampaignSuccess(now)) {
        return *success;
    }

    // (2) Retry interval not yet elapsed — stay CONNECTING, no new strategy yet.
    if (!shouldRetryWiFi(WiFiState::State::WIFI_CONNECTING, now,
                         owner_.ctx_.lastRetryMs, owner_.ctx_.reconnectAttempts)) {
        return StateTransition(owner_.ctx_.state);
    }

    // (3) Apply the current strategy, advance the rotation.
    applyCurrentStrategy(now);
    advanceRotation(now);

    // (4) Escalate to AP if every strategy/loop combination has been attempted.
    if (auto exhausted = checkExhaustion()) {
        return *exhausted;
    }

    return StateTransition(owner_.ctx_.state);  // stay CONNECTING until exhausted/connected
}

// WiFiManager implementation

WiFiManager::WiFiManager(IWiFi& wifi, IPreferences& prefs, ISerial& serial,
                         const char* bakedSsid, const char* bakedPass)
    : wifi_(wifi), prefs_(prefs), serial_(serial)
    , bakedSsid_(bakedSsid), bakedPass_(bakedPass) {
    // Initialize state handlers (RECONNECTING merged into connectingHandler_)
    disconnectedHandler_ = std::make_unique<DisconnectedStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    connectingHandler_ = std::make_unique<ConnectingStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_,
        [this]() {
            if (discoveryResetCallback_) {
                discoveryResetCallback_();
            }
        });
    connectedStaHandler_ = std::make_unique<ConnectedStaStateHandler>(wifi_);
    connectedApHandler_ = std::make_unique<ConnectedApStateHandler>(wifi_, prefs_, bakedSsid_, bakedPass_);
    // AuthCampaign owns the resilient-auth strategy rotation; see class comment
    // for the SRP rationale. Created last so it can capture references to the
    // already-initialised collaborators via `*this`.
    authCampaign_ = std::make_unique<AuthCampaign>(wifi_, prefs_, serial_,
                                                   *this, bakedSsid_, bakedPass_);
}

void WiFiManager::init() {
    ctx_.state = WiFiState::State::WIFI_DISCONNECTED;
    update(0);  // Initial state machine tick
}

void WiFiManager::update(uint32_t now) {
    // RESILIENT AUTH (req-1/2): while an auth-mechanism failure campaign is in
    // progress we do NOT fall back to AP mode on the first (or first few) auth
    // failures. We rotate through progressively-harder reset/retry STRATEGIES
    // (best-first, least-good last) and loop the whole list
    // WIFI_AUTH_STRATEGY_LOOP_COUNT times, escalating to AP mode ONLY once every
    // connection opportunity is EXHAUSTED (bounded, not infinite). Run this
    // BEFORE the normal handler so the chosen strategy is actually applied to
    // the radio each retry tick, and so AP escalation overrides the connect loop.
    if (ctx_.pendingAuthFail) {
        // The campaign fully owns the retry loop while active: it applies the
        // rotating strategy, detects a genuine connect (to reset + transition),
        // and escalates to AP when exhausted. The normal CONNECTING handler must
        // NOT also issue begin() here, so we short-circuit on the campaign.
        StateTransition campaign = runAuthCampaign(now);
        applyStateTransition(campaign);
        return;
    }

    IWiFiStateHandler* handler = getStateHandler(ctx_.state);
    StateTransition transition = handler->execute(now, ctx_);
    applyStateTransition(transition);
}

StateTransition WiFiManager::runAuthCampaign(uint32_t now) {
    // Delegate to the AuthCampaign member — it owns the strategy rotation
    // while a campaign is active. The public observable behaviour is
    // identical to the pre-extraction monolithic implementation; see
    // AuthCampaign::tick() for the step-by-step ordering.
    return authCampaign_->tick(now);
}

void WiFiManager::fireDiscoveryResetCallback() {
    // NOP if no callback is wired (headless tests). Mirrors the inline
    // discovery-reset guard in ConnectingStateHandler::handleConnected().
    if (discoveryResetCallback_) {
        discoveryResetCallback_();
    }
}

bool WiFiManager::factoryReset() {
    return clearCredentials();
}

bool WiFiManager::storeCredentials(const std::string& ssid, const std::string& pass) {
    prefs_.begin(WiFiConfig::NVS_WIFI_NAMESPACE, false);
    // Write cred_count first so hasStoredCredentials / determineCredentialSource
    // can gate on a single integer rather than per-key length probes.
    bool success = prefs_.putString(WiFiConfig::NVS_WIFI_CRED_COUNT, "1") > 0;
    success = success && (prefs_.putString(WiFiConfig::NVS_WIFI_SSID, ssid) > 0);
    success = success && (prefs_.putString(WiFiConfig::NVS_WIFI_PASS, pass) > 0);
    prefs_.end();
    return success;
}

bool WiFiManager::hasStoredCredentials() const {
    IPreferences& nonConstPrefs = const_cast<IPreferences&>(prefs_);
    nonConstPrefs.begin(WiFiConfig::NVS_WIFI_NAMESPACE, true);
    size_t countLen = nonConstPrefs.getBytesLength(WiFiConfig::NVS_WIFI_CRED_COUNT);
    nonConstPrefs.end();
    return (countLen > 0);
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
    // Captured before any mutation so the trace reports the true origin state.
    const WiFiState::State from = ctx_.state;
    ctx_.lastDisconnectReason = reason;

    //#region RESILIENT AUTH (req-1): an auth-mechanism failure is NOT proof of a wrong password.
    // The reason codes below were previously treated as "definitive wrong
    // credential → instant AP mode", but the verified field diagnosis shows
    // that is wrong: the password for "manht2" is CORRECT (a button-reset
    // connects), so a 202/15/23 here is a spurious/transient or wrong-mechanism
    // failure (radio/PHY not settled, or WPA2-vs-WPA3/SAE mismatch) — and at
    // this layer we CANNOT distinguish wrong-PSK from wrong-mechanism. Therefore
    // we must NOT give up. Instead we arm an auth-failure CAMPAIGN: the
    // ConnectingStateHandler now rotates through progressively-harder
    // reset/retry STRATEGIES (best-first, least-good last) and loops the whole
    // list WIFI_AUTH_STRATEGY_LOOP_COUNT (3) times before escalating to AP mode.
    // This mirrors what a manual button-reset achieves (which works), and only
    // gives up once connection opportunities are EXHAUSTED — bounded, not infinite.
    //
    //   - 4WAY_HANDSHAKE_TIMEOUT (15): handshake never completed
    //   - 802_1X_AUTH_FAILED (23): enterprise auth rejected
    //   - AUTH_FAIL (202): bad PSK-class
    //
    // Transient session/assoc lifecycle reasons that are RECOVERABLE by a fresh
    // reconnect — AUTH_EXPIRE(2), AUTH_LEAVE(3), BEACON_TIMEOUT(200),
    // ASSOC_EXPIRE(4), CIPHER_SUITE_REJECTED(24), reason=0/4/204 etc. — fall
    // through below so the stack re-associates instead of abandoning STA.
    //#endregion
    if (isAuthMechanismFailure(reason)) {
        ctx_.pendingAuthFail = true;
        ctx_.lastDisconnectReason = reason;
        // Clear the TCP-restart flag: an auth-fail campaign is NOT a confirmed
        // drop that needs a socket re-bind; a later genuine connect re-arms it.
        ctx_.tcpServerNeedsRestart = false;
        // Reset the rotation to the best (least-disruptive) strategy at the start
        // of each fresh auth-failure event; the loop counter is preserved across
        // events so multiple failures progress through the campaign rather than
        // restarting it. (If a previous campaign already exhausted, the loop
        // counter is past the cap, so a brand-new 202 re-enters the campaign but
        // re-escalates to AP on the very next tick — the loop guard at
        // isAuthCampaignExhausted() fires immediately. That is correct: a second
        // distinct auth failure after exhaustion should give up again at once,
        // not restart a fresh 3×3 campaign.)
        if (ctx_.authFailStrategyLoop == 0) {
            ctx_.authFailStrategyIndex = 0;
        }
        // Ensure we are in the CONNECTING state so the retry/campaign loop runs.
        // (If the event arrived while CONNECTED, behave like a transient drop and
        // re-enter WIFI_CONNECTING; do NOT arm tcpServerNeedsRestart here — a later
        // genuine connect resets via its own path.)
        if (ctx_.state == WiFiState::State::WIFI_CONNECTED) {
            ctx_.state = WiFiState::State::WIFI_CONNECTING;
            ctx_.reconnectAttempts = 0;  // Restart aggressive-first-retries window (req-1)
        }
        ctx_.lastRetryMs = 0;  // Arm the retry so the first strategy fires promptly
        // Single trace line: captures the true origin state and the campaign
        // outcome (reason + strategy/loop counters) so "why still connecting"
        // is answerable at a glance.
        serial_.printf("[STATE] WiFi: %s -> WIFI_CONNECTING (auth fail reason=%d strategy=%d/%u loop=%d/%u)\r\n",
                       stateName(from), reason,
                       ctx_.authFailStrategyIndex,
                       WiFiConfig::WIFI_AUTH_STRATEGY_COUNT,
                       ctx_.authFailStrategyLoop,
                       WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
        // LED pattern is now owned by FirmwareApp via selectLedPattern.
        return;
    }

    if (ctx_.state == WiFiState::State::WIFI_CONNECTED) {
        ctx_.state = WiFiState::State::WIFI_CONNECTING;
        ctx_.tcpServerNeedsRestart = true;
        ctx_.lastRetryMs = 0;  // Will be set on next update
        ctx_.reconnectAttempts = 0;  // Restart aggressive-first-retries window (req-1)
        // Transient/recoverable disconnect: the stack re-associates rather than
        // abandoning STA. Logged as RECONNECTING because that is the semantic
        // role of this re-entry into WIFI_CONNECTING from a live connection
        // (the dedicated RECONNECTING state was merged into WIFI_CONNECTING).
        serial_.printf("[STATE] WiFi: %s -> RECONNECTING (reason=%d)\r\n",
                       stateName(from), reason);
    }
}

const char* WiFiManager::stateName(WiFiState::State state) {
    switch (state) {
        case WiFiState::State::WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
        case WiFiState::State::WIFI_CONNECTING: return "WIFI_CONNECTING";
        case WiFiState::State::WIFI_CONNECTED: return "WIFI_CONNECTED";
        case WiFiState::State::WIFI_AP_MODE_DEFAULT: return "WIFI_AP_MODE_DEFAULT";
        case WiFiState::State::WIFI_AP_MODE_AUTH_FAIL: return "WIFI_AP_MODE_AUTH_FAIL";
        default: return "UNKNOWN";
    }
}

std::string WiFiManager::resolveTargetSsid() const {
    std::string storedSsid;
    std::string storedPass;
    if (loadCredentialsImpl(prefs_, storedSsid, storedPass) && !storedSsid.empty()) {
        return storedSsid;
    }
    if (bakedSsid_ && bakedSsid_[0] != '\0') {
        return std::string(bakedSsid_);
    }
    return std::string();
}

std::string WiFiManager::getAuthCampaignDetail() const {
    if (!ctx_.pendingAuthFail) {
        return std::string();
    }
    char buf[128]{};
    std::snprintf(buf, sizeof(buf),
                  "auth fail reason=%d strategy=%d/%u loop=%d/%u",
                  ctx_.lastDisconnectReason,
                  ctx_.authFailStrategyIndex,
                  WiFiConfig::WIFI_AUTH_STRATEGY_COUNT,
                  ctx_.authFailStrategyLoop,
                  WiFiConfig::WIFI_AUTH_STRATEGY_LOOP_COUNT);
    return std::string(buf);
}

// Testable pure functions - namespace-level for testability

CredentialSource determineCredentialSource(IPreferences& prefs, const char* bakedSsid, const char* bakedPass) {
    prefs.begin(WiFiConfig::NVS_WIFI_NAMESPACE, true);
    size_t countLen = prefs.getBytesLength(WiFiConfig::NVS_WIFI_CRED_COUNT);
    prefs.end();

    if (countLen > 0) {
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

bool shouldRetryWiFi(WiFiState::State state, uint32_t now, uint32_t lastRetry, uint32_t reconnectAttempts) {
    // WIFI_CONNECTING covers both first-connect and reconnect (RECONNECTING merged).
    if (state != WiFiState::State::WIFI_DISCONNECTED &&
        state != WiFiState::State::WIFI_CONNECTING) {
        return false;
    }
    // RESILIENT RECONNECT (req-1): on a dropped STA connection, retry the last WiFi
    // IMMEDIATELY for the first few reconnect attempts (no backoff). A WiFi radio
    // reset on the ESP32 typically re-associates on the very next begin(), so a long
    // 5s backoff needlessly extends the mid-drive connectivity gap. After the
    // aggressive window passes, fall back to the normal retry interval.
    const uint32_t intervalMs = (reconnectAttempts < WiFiConfig::WIFI_CONNECT_FIRST_RETRIES_COUNT)
        ? WiFiConfig::WIFI_CONNECT_FIRST_RETRIES_MS
        : WiFiConfig::WIFI_CONNECT_RETRY_INTERVAL_MS;
    return (now - lastRetry) >= intervalMs;
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

bool shouldRestartTcpServerForReconnect(const std::string& newIp, const std::string& lastConnectedIp, uint32_t outageMs) {
    // IP-aware restart decision (restored after the ddd8239 regression).
    //
    // ddd8239 changed this to unconditionally `return true`, i.e. re-begin() the
    // listening TCP socket on EVERY (re)connect transition. On a flapping STA
    // (e.g. manht2) that fires an end()/begin() of the listening socket on every
    // WiFi blip — which tears down the listening port mid-handshake, so a host
    // AUTH never gets its "OK" and the connection is dropped (client_connected
    // then an immediate reason=0 disconnect). Restoring the IP-aware test keeps
    // the socket bound across same-IP blips (the listening socket DOES survive a
    // brief radio reset on the ESP32) while still re-binding when the STA IP
    // actually changes or the outage was long enough that the socket is stale.
    //
    // Resilience is preserved: this only governs *whether the listening socket is
    // re-bound*; the reconnect/rapid-retry, discovery-reset, and always-allow
    // connect behaviour are unaffected. The client-stop guard in
    // restartTcpServerIfNeeded() additionally ensures a live, adopted client is
    // never stomped even when a restart does occur.
    return lastConnectedIp.empty() || newIp != lastConnectedIp || outageMs > WiFiConfig::LONG_OUTAGE_MS;
}

void WiFiManager::applyStateTransition(const StateTransition& transition) {
    // Treat "stay in current state" as idempotent no-op (regardless of which state)
    if (transition.nextState == ctx_.state) {
        return;  // No transition - stay sentinel
    }

    // Capture the STA IP at the single authoritative entry point into WIFI_CONNECTED.
    // This is the last-known IP for the next reconnect's IP-aware tcpRestart check.
    // Checked BEFORE the state assignment so the "entering CONNECTED" condition is
    // evaluated against the pre-transition state.
    if (transition.nextState == WiFiState::State::WIFI_CONNECTED &&
        ctx_.state != WiFiState::State::WIFI_CONNECTED) {
        ctx_.lastConnectedIp = wifi_.localIP();
    }

    const WiFiState::State from = ctx_.state;
    ctx_.state = transition.nextState;

    // AP-MODE RECOVERY: arm the periodic STA retry timer on entry to an AP
    // state. The ConnectedApStateHandler consults this to decide when to
    // re-attempt STA. Reset here (rather than in the handler) so any path into
    // AP mode (escalation, factory reset, fresh boot) gets a fresh timer.
    if (WiFiState::isApModeState(transition.nextState) &&
        !WiFiState::isApModeState(from)) {
        ctx_.apModeStaRetryMs = 0;
    }

    // Every state-machine-driven transition is traced here. The early-return
    // above guarantees from != ctx_.state, so this never logs a self-transition.
    serial_.printf("[STATE] WiFi: %s -> %s\r\n", stateName(from), stateName(ctx_.state));

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
        case WiFiState::State::WIFI_AP_MODE_DEFAULT: return connectedApHandler_.get();
        case WiFiState::State::WIFI_AP_MODE_AUTH_FAIL: return connectedApHandler_.get();
        default: return disconnectedHandler_.get();
    }
}

} // namespace esp32_firmware