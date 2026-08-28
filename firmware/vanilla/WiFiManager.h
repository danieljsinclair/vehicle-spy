#pragma once

// WiFiManager.h - Vanilla C++ WiFi lifecycle management
// Extracted from can-bridge.ino for host testability

#include <cstdint>
#include <string>
#include <functional>
#include <array>
#include <memory>
#include <iostream>
#include <optional>

namespace esp32_firmware {

// WiFi state machine
namespace WiFiState {
    // Ordinals are load-bearing: selectLedPattern() maps the state (as int) to an
    // LED pattern, so keep these stable and contiguous.
    enum class State {
        WIFI_DISCONNECTED = 0,
        WIFI_CONNECTING = 1,
        WIFI_CONNECTED = 2,
        WIFI_AP_MODE_DEFAULT = 3,   // AP because no SSID was ever configured
        WIFI_AP_MODE_AUTH_FAIL = 4  // AP because connecting with credentials failed
    };

    // State-model knowledge: true for both AP states. FirmwareApp uses this
    // instead of inline ordinal comparisons (OCP).
    inline bool isApModeState(State s) {
        return s == State::WIFI_AP_MODE_DEFAULT || s == State::WIFI_AP_MODE_AUTH_FAIL;
    }

    struct Context {
        State state = State::WIFI_DISCONNECTED;
        uint32_t lastRetryMs = 0;
        uint32_t connectStartTime = 0;
        int lastDisconnectReason = 0;  // wifi_err_reason_t
        bool tcpServerNeedsRestart = false;
        int escalatedToApReason = 0;  // wifi_err_reason_t that triggered AP fallback (0 = not escalated)
        std::string lastConnectedIp;    // STA IP captured at the last WIFI_CONNECTED entry (empty before first connect)
        bool reconnectPending = false;   // true after a drop, until the re-connect IP check resolves
        uint32_t disconnectStartMs = 0;  // timestamp of the most recent drop (for outage-duration safety check)
        uint32_t reconnectAttempts = 0;  // consecutive reconnect (re-begin) attempts since the last drop
        // RESILIENT AUTH (firmware bug fix): an auth-failure reason (AUTH_FAIL 202,
        // 4WAY_HANDSHAKE_TIMEOUT 15, 802_1X_AUTH_FAILED 23) is NOT proof of a wrong
        // password — at this layer we cannot distinguish wrong-PSK from a
        // wrong-mechanism (e.g. WPA3/SAE rejecting a WPA2 client) or a transient.
        // So we must exhaust connection opportunities BEFORE escalating to AP mode.
        // While an auth-fail campaign is active we rotate through progressively
        // harder reset/retry STRATEGIES (best-first, least-good last) and loop
        // that list a bounded number of times. These counters are reset on a
        // successful WL_CONNECTED so a genuine later drop starts fresh.
        bool pendingAuthFail = false;  // an auth-mechanism failure is in progress; rotate strategies instead of bailing
        int authFailStrategyIndex = 0; // index into the strategy list (0 = best/least-disruptive)
        int authFailStrategyLoop = 0;  // how many full passes through the strategy list we have made
        // AP-MODE RECOVERY: timestamp of the last attempt to re-associate from
        // WIFI_AP_MODE_AUTH_FAIL. Set to 0 when entering the AP state so the
        // first retry attempt happens promptly (one full interval later). Only
        // consulted in WIFI_AP_MODE_AUTH_FAIL (not DEFAULT — there is nothing
        // to retry if no credentials are configured).
        uint32_t apModeStaRetryMs = 0;
    };
}

// Credential source enumeration
enum class CredentialSource {
    NONE,
    STORED_NVS,
    BAKED_IN
};

// WiFi state transition result
struct StateTransition {
    WiFiState::State nextState;
    bool setTcpServerRestartFlag;
    bool initNtp;
    const char* message;

    StateTransition() : nextState(WiFiState::State::WIFI_DISCONNECTED),
                       setTcpServerRestartFlag(false), initNtp(false), message(nullptr) {}

    explicit StateTransition(WiFiState::State state, bool tcpRestart = false, bool ntp = false, const char* msg = nullptr)
        : nextState(state), setTcpServerRestartFlag(tcpRestart), initNtp(ntp), message(msg) {}
};

// WiFi state handler interface (State Pattern)
struct IWiFiStateHandler {
    virtual StateTransition execute(uint32_t now, WiFiState::Context& ctx) = 0;
    virtual ~IWiFiStateHandler() = default;
};

// Opaque WiFi event-info payload (models the platform's WiFiEventInfo_t, e.g.
// ESP-IDF arduino_event_info_t, without pulling the Arduino header into this
// host-compilable interface). Implementations (ArduinoWiFi) and mocks supply
// the concrete struct; the interface only names the pointer meaningfully.
struct WifiEventInfo;

// WiFi interface (abstraction for unit testing)
struct IWiFi {
    virtual void setMode(int mode) = 0;
    virtual void begin(const char* ssid, const char* pass) = 0;
    virtual void disconnect(bool wifiOff, bool eraseAP) = 0;
    virtual int status() const = 0;
    virtual std::string localIP() const = 0;
    virtual std::string softAPIP() const = 0;
    virtual void softAP(const char* ssid, const char* pass) = 0;
    virtual void setHostname(const char* name) = 0;
    virtual int getMode() const = 0;
    virtual std::string SSID() const = 0;
    virtual const char* disconnectReasonName(int reason) const = 0;
    // AP BSSID the STA is (or was last) associated with, as "aa:bb:cc:dd:ee:ff".
    // Empty when not associated. Used to DETECT Deco mesh node-bouncing: if the
    // BSSID changes between connect attempts the 2.4GHz-only ESP32 is being
    // steered between mesh nodes it cannot follow (a common cause of reason=8/39
    // connect loops). Host mock returns an injected value.
    virtual std::string BSSID() const = 0;
    // RSSI (dBm) of the AP the STA is associated with. 0 when not associated.
    // Surfaces weak-signal auth/handshake failures (reason=204) at a glance.
    virtual int8_t RSSI() const = 0;
    virtual void onEvent(std::function<void(int, WifiEventInfo*)> cb, int event) = 0;
    virtual ~IWiFi() = default;
};

// Preferences interface (NVS storage abstraction)
struct IPreferences {
    virtual void begin(const char* name, bool readOnly) = 0;
    virtual void end() = 0;
    virtual size_t getBytesLength(const char* key) = 0;
    virtual std::string getString(const char* key, const std::string& defaultValue = "") = 0;
    virtual size_t putString(const char* key, const std::string& value) = 0;
    virtual void clear() = 0;
    virtual ~IPreferences() = default;
};

// StatusLED interface for pattern updates and animation
struct IStatusLED {
    virtual void setPattern(int pattern) = 0;  // Pattern enum from StatusLED
    virtual void update(uint32_t now) = 0;     // Drive LED animation (call each tick)
    virtual ~IStatusLED() = default;
};

// Serial debug-output interface (DI-injected for testability).
//
// This is a LOW-LEVEL transport seam for WiFi state-transition tracing, distinct
// from IEventLogger (ISerialEventLogger.h), which carries the structured
// [EVENT]/[STATE] observability contract owned by FirmwareApp. ISerial exists so
// WiFiManager can emit human-readable transition traces (including the disconnect
// reason code) without a hard dependency on Arduino's global Serial object.
//
// printf is variadic to match the Arduino Serial API it adapts; the format
// attribute lets -Wformat verify call sites at compile time (index 2/3 because
// index 1 is the implicit `this`).
struct ISerial {
    virtual void println(const char* msg) = 0;

    __attribute__((format(printf, 2, 3)))
    virtual void printf(const char* fmt, ...) = 0;

    virtual ~ISerial() = default;
};

// Configuration constants
struct WiFiConfig {
    static constexpr uint32_t WIFI_CONNECT_RETRY_INTERVAL_MS = 5000;
    // RESILIENT RECONNECT (req-1): first N reconnect attempts retry the last WiFi
    // IMMEDIATELY (minimal/no backoff) so a dropped STA connection is re-associated
    // without waiting. The radio reset on the ESP32 often succeeds on the very next
    // begin(), so a long 5s backoff needlessly extends a mid-drive connectivity gap.
    static constexpr uint32_t WIFI_CONNECT_FIRST_RETRIES_MS = 0;
    static constexpr uint32_t WIFI_CONNECT_FIRST_RETRIES_COUNT = 5;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t WIFI_INITIAL_CONNECT_MAX_RETRIES = 60;  // 5 minutes at 5s interval
    // AP-MODE RECOVERY: once the device has escalated to WIFI_AP_MODE_AUTH_FAIL
    // (real auth rejection, campaign exhausted), periodically try STA again so
    // the device self-heals when the operator fixes the password or the AP
    // reboots with the same credentials. Does NOT apply to WIFI_AP_MODE_DEFAULT
    // (no creds configured — there is nothing to retry).
    static constexpr uint32_t WIFI_AP_MODE_STA_RETRY_INTERVAL_MS = 300000;  // 5 min
    // RESILIENT AUTH (firmware bug fix): an auth-failure reason (AUTH_FAIL 202,
    // 4WAY_HANDSHAKE_TIMEOUT 15, 802_1X_AUTH_FAILED 23) is NOT proof of a wrong
    // password. The same code can be triggered by a wrong AUTH MECHANISM (e.g. a
    // WPA3/SAE AP rejecting a WPA2-only client) or a transient. We cannot
    // distinguish wrong-password from wrong-mechanism at this layer, so — per
    // requirement — we must exhaust connect opportunities BEFORE giving up.
    //
    // The ESP32 Arduino core in use (2.0.17) has NO WiFi.setMinSecurity / WPA3 /
    // SAE API, so literal WPA2->WPA3 rotation is impossible. Instead the
    // "different connection options" are progressively-harder reset/retry
    // STRATEGIES that mirror what a manual button-reset achieves (which works):
    //   strategy 0 (best):   plain WiFi.begin() re-attempt
    //   strategy 1 (mid):    disconnect() + STA mode reset, then begin()
    //   strategy 2 (worst):  full radio cycle — mode OFF, then STA, then begin()
    // We rotate best-first / least-good-last through the list, and loop the WHOLE
    // list WIFI_AUTH_STRATEGY_LOOP_COUNT (3) times. ONLY after all loops are
    // exhausted do we escalate to AP mode. This is bounded, not infinite.
    static constexpr uint32_t WIFI_AUTH_STRATEGY_COUNT = 3;     // number of strategies in the list
    static constexpr uint32_t WIFI_AUTH_STRATEGY_LOOP_COUNT = 3; // full list passes before AP escalation
    static constexpr const char* AP_SSID = "ESP32-CAN";
    static constexpr const char* AP_PASS = "cancan12";
    static constexpr const char* NVS_WIFI_NAMESPACE = "wifi";
    // List-shaped schema (phase-2 additive): cred_count + indexed entry[0].
    // Pinned here so phase-2 cannot silently change the on-disk key shape.
    static constexpr const char* NVS_WIFI_CRED_COUNT = "cred_count";
    static constexpr const char* NVS_WIFI_SSID = "ssid_0";
    static constexpr const char* NVS_WIFI_PASS = "pass_0";
    static constexpr const char* NVS_TOKEN_NAMESPACE = "auth";
    static constexpr const char* NVS_TOKEN_KEY = "token";
    static constexpr const char* HOSTNAME = "esp32-can";
    static constexpr uint32_t LONG_OUTAGE_MS = 30000;  // safety: restart TCP server after this disconnect duration even if IP is unchanged
};

// Forward declaration: full definition later in this header (after
// WiFiManager). WiFiManager holds it by unique_ptr, which only needs the
// complete type at the destructor instantiation point (the .cpp).
class AuthCampaign;

// WiFiManager - orchestrates WiFi state machine and credentials
class WiFiManager {
    // AuthCampaign is a private collaborator — it needs to read/write the
    // shared connect-loop fields on the Context (pendingAuthFail, strategy
    // counters, retry/attempt counters, IP-tracking fields, escalation
    // reason). Friendship scopes that access without widening the public API.
    friend class AuthCampaign;

public:
    // Callback for NTP initialization
    using NtpInitCallback = std::function<void()>;

    // Callback for TCP server restart notification
    using TcpServerRestartCallback = std::function<void()>;

    // Callback invoked on (re)connect to reset discovery backoff so the app is
    // found at the possibly-new IP quickly (resilient-reconnect req-2).
    using DiscoveryResetCallback = std::function<void()>;

    WiFiManager(IWiFi& wifi, IPreferences& prefs, ISerial& serial,
                const char* bakedSsid = nullptr, const char* bakedPass = nullptr);

    // Initialize the WiFi state machine
    void init();

    // Main update loop - call from main loop()
    void update(uint32_t now);

    // Get current WiFi state
    WiFiState::State getState() const { return ctx_.state; }

    // Get current context (for testing)
    const WiFiState::Context& getContext() const { return ctx_; }

    // Set callbacks
    void setNtpInitCallback(NtpInitCallback cb) { ntpInitCallback_ = std::move(cb); }
    void setTcpServerRestartCallback(TcpServerRestartCallback cb) { tcpServerRestartCallback_ = std::move(cb); }
    void setDiscoveryResetCallback(DiscoveryResetCallback cb) { discoveryResetCallback_ = std::move(cb); }

    // Factory reset - clear stored credentials
    bool factoryReset();

    // Store WiFi credentials to NVS
    bool storeCredentials(const std::string& ssid, const std::string& pass);

    // Check if we have stored credentials
    bool hasStoredCredentials() const;

    // Load stored credentials
    bool loadCredentials(std::string& ssid, std::string& pass) const;

    // Clear stored credentials
    bool clearCredentials();

    // Handle WiFi disconnected event (called from WiFi event callback)
    void onDisconnected(int reason);

    // Check if TCP server restart is needed
    bool shouldRestartTcpServer() const { return ctx_.tcpServerNeedsRestart; }
    void clearTcpServerRestartFlag() { ctx_.tcpServerNeedsRestart = false; }

    // Get state name for logging
    static const char* stateName(WiFiState::State state);

    // Resolve the SSID the manager is currently trying to associate with
    // (stored NVS if present, otherwise baked-in). Reported on the [STATE]
    // heartbeat so "which SSID" is answerable at a glance.
    std::string resolveTargetSsid() const;

    // If an auth-mechanism failure campaign is in progress, return a human string
    // describing the current retry state, e.g. "auth fail reason=202 strategy=1/3
    // loop=0/3"; empty string otherwise. Reported on the [STATE] heartbeat.
    std::string getAuthCampaignDetail() const;

private:
    IWiFi& wifi_;
    IPreferences& prefs_;
    ISerial& serial_;
    const char* bakedSsid_;
    const char* bakedPass_;

    WiFiState::Context ctx_;
    NtpInitCallback ntpInitCallback_;
    TcpServerRestartCallback tcpServerRestartCallback_;
    DiscoveryResetCallback discoveryResetCallback_;

    // State handlers (RECONNECTING merged into connectingHandler_)
    std::unique_ptr<IWiFiStateHandler> disconnectedHandler_;
    std::unique_ptr<IWiFiStateHandler> connectingHandler_;
    std::unique_ptr<IWiFiStateHandler> connectedStaHandler_;
    std::unique_ptr<IWiFiStateHandler> connectedApHandler_;

    void applyStateTransition(const StateTransition& transition);
    IWiFiStateHandler* getStateHandler(WiFiState::State state);

    // Drive the resilient-auth strategy rotation while a campaign is active
    // (pendingAuthFail == true). Delegates to the AuthCampaign member, which
    // owns the strategy index/loop counters and applies the rotating
    // reset/retry strategies. Returns a transition; when its nextState equals
    // the current state AND neither flag is set, the caller falls through to
    // the normal CONNECTING handler (retry loop stays alive underneath).
    StateTransition runAuthCampaign(uint32_t now);

    // Discovery-backoff reset (resilient-reconnect req-2). Exposed so
    // AuthCampaign can fire it on a successful mid-campaign connect without
    // making AuthCampaign a friend of every WiFiManager state.
    void fireDiscoveryResetCallback();

    // Read-only view of the connect-loop counter, for AuthCampaign's tick().
    uint32_t reconnectAttempts() const { return ctx_.reconnectAttempts; }

    // AuthCampaign member — owns the resilient-auth strategy rotation while a
    // campaign is in progress. See AuthCampaign for the SRP rationale.
    std::unique_ptr<AuthCampaign> authCampaign_;
};

// Testable pure functions - standalone in namespace for testability
CredentialSource determineCredentialSource(IPreferences& prefs, const char* bakedSsid, const char* bakedPass);
bool shouldFallbackToApMode(CredentialSource source, uint32_t connectDurationMs);
bool isInitialConnectTimeout(uint32_t connectDurationMs);
bool shouldRetryWiFi(WiFiState::State state, uint32_t now, uint32_t lastRetry, uint32_t reconnectAttempts);
bool loadCredentialsImpl(IPreferences& prefs, std::string& ssid, std::string& pass);
bool shouldRestartTcpServerForReconnect(const std::string& newIp, const std::string& lastConnectedIp, uint32_t outageMs);

// RESILIENT AUTH pure helpers (testable without hardware):
// True for the reason codes that indicate an auth-mechanism failure we MUST
// exhaust strategies for before giving up (NOT treated as wrong-password).
bool isAuthMechanismFailure(int reason);
// LINK-LEVEL / RECOVERABLE drops (testable without hardware):
// True for reason codes that mean "the AP is unreachable, keep retrying" —
// e.g. a mesh AP rebooting delivers reason 0/200/201 in rapid succession. These
// MUST NOT trigger AP-mode escalation; the mesh will be back in 2-30 minutes and
// the device will re-associate naturally. The complementary classification to
// isAuthMechanismFailure() — together they partition the disconnect-reason
// space: auth failures exhaust a campaign; link-level drops retry forever.
bool isLinkLevelDrop(int reason);
// RESILIENT AUTH: named strategy values (the index used to rotate through
// progressively-harder reset/retry strategies when an auth failure is in
// progress). The integer values are load-bearing: applyAuthStrategy() and
// WiFiConfig::WIFI_AUTH_STRATEGY_COUNT both index by ordinal. Keep the
// enumerators contiguous starting at 0 and stable.
enum class AuthStrategy : int {
    BestReassociate = 0,  // plain WiFi.begin() re-association (least disruptive)
    ResetStaMode = 1,     // disconnect() + STA mode reset, then begin()
    FullRadioCycle = 2    // full radio OFF/STA cycle + begin() (most disruptive)
};
// Apply the reset/retry strategy at `index` to the IWiFi abstraction using the
// resolved credentials. Index is clamped to [0, WIFI_AUTH_STRATEGY_COUNT).
void applyAuthStrategy(IWiFi& wifi, CredentialSource source,
                       const std::string& storedSsid, const std::string& storedPass,
                       const char* bakedSsid, const char* bakedPass, int index);
// Given the current strategy index/loop and the configured counts, return true
// when ALL connection opportunities are exhausted (escalate to AP mode).
bool isAuthCampaignExhausted(int strategyIndex, int loopIndex);

// Forward-declared so AuthCampaign can hold a reference for the
// (re)connect-time discovery backoff reset.
class WiFiManager;

// AuthCampaign — owns the resilient-auth rotation logic for the duration of an
// auth-failure campaign. SRP extraction: the campaign's strategy index/loop
// counters, "apply current strategy + advance rotation" step, and exhaustion
// check are a distinct responsibility from the WiFi state machine itself.
//
// The campaign fully owns the retry loop while active: it applies the rotating
// strategy, detects a genuine connect (to reset + transition), and escalates
// to AP when exhausted. The normal CONNECTING handler must NOT also issue
// begin() during the campaign, so WiFiManager::update() short-circuits to
// AuthCampaign::tick() while pendingAuthFail is set.
//
// Cross-cutting state (shared with the normal connect loop) stays in
// WiFiManager::Context: pendingAuthFail flag, lastRetryMs, reconnectAttempts,
// disconnectStartMs, lastConnectedIp, escalatedToApReason, lastDisconnectReason.
// AuthCampaign reads/writes these directly through WiFiManager so the public
// observable behaviour is identical to the pre-extraction monolithic
// runAuthCampaign().
class AuthCampaign {
public:
    AuthCampaign(IWiFi& wifi, IPreferences& prefs, ISerial& serial,
                 WiFiManager& owner, const char* bakedSsid, const char* bakedPass);

    // One retry tick. Returns a transition; when nextState equals the current
    // state AND neither flag is set, the caller falls through to the normal
    // CONNECTING handler (retry loop stays alive underneath).
    StateTransition tick(uint32_t now);

private:
    // Handle the "already connected mid-campaign" case: reset the rotation
    // and transition to CONNECTED (tcpRestart + NTP init, same as the normal
    // handler). Returns nullopt when not yet connected.
    std::optional<StateTransition> checkCampaignSuccess(uint32_t now);

    // Apply the current strategy to the radio + trace.
    void applyCurrentStrategy(uint32_t now);

    // Advance the strategy index (wrap within the list) and the loop counter
    // (advance on full-wrap). Updates lastRetryMs + reconnectAttempts.
    void advanceRotation(uint32_t now);

    // Returns a transition to WIFI_AP_MODE_AUTH_FAIL when the campaign has
    // exhausted every strategy/loop combination, std::nullopt otherwise.
    std::optional<StateTransition> checkExhaustion();

    IWiFi& wifi_;
    IPreferences& prefs_;
    ISerial& serial_;
    WiFiManager& owner_;
    const char* bakedSsid_;
    const char* bakedPass_;
};

} // namespace esp32_firmware