#pragma once

// WiFiManager.h - Vanilla C++ WiFi lifecycle management
// Extracted from can-bridge.ino for host testability

#include <cstdint>
#include <string>
#include <functional>
#include <array>
#include <memory>

namespace esp32_firmware {

// WiFi state machine
namespace WiFiState {
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED_STA,
        CONNECTED_AP,
        RECONNECTING
    };

    struct Context {
        State state = State::DISCONNECTED;
        uint32_t lastRetryMs = 0;
        uint32_t connectStartTime = 0;
        int lastDisconnectReason = 0;  // wifi_err_reason_t
        bool tcpServerNeedsRestart = false;
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

    StateTransition() : nextState(WiFiState::State::DISCONNECTED),
                       setTcpServerRestartFlag(false), initNtp(false), message(nullptr) {}

    explicit StateTransition(WiFiState::State state, bool tcpRestart = false, bool ntp = false, const char* msg = nullptr)
        : nextState(state), setTcpServerRestartFlag(tcpRestart), initNtp(ntp), message(msg) {}
};

// WiFi state handler interface (State Pattern)
struct IWiFiStateHandler {
    virtual StateTransition execute(uint32_t now, WiFiState::Context& ctx) = 0;
    virtual ~IWiFiStateHandler() = default;
};

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
    virtual void onEvent(std::function<void(int, void*)> cb, int event) = 0;
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

// Configuration constants
struct WiFiConfig {
    static constexpr uint32_t WIFI_CONNECT_RETRY_INTERVAL_MS = 5000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t WIFI_INITIAL_CONNECT_MAX_RETRIES = 60;  // 5 minutes at 5s interval
    static constexpr const char* AP_SSID = "ESP32-CAN";
    static constexpr const char* AP_PASS = "cancan12";
    static constexpr const char* NVS_WIFI_NAMESPACE = "wifi";
    static constexpr const char* NVS_WIFI_SSID = "ssid";
    static constexpr const char* NVS_WIFI_PASS = "pass";
    static constexpr const char* HOSTNAME = "esp32-can";
};

// WiFiManager - orchestrates WiFi state machine and credentials
class WiFiManager {
public:
    // Callback for NTP initialization
    using NtpInitCallback = std::function<void()>;

    // Callback for TCP server restart notification
    using TcpServerRestartCallback = std::function<void()>;

    WiFiManager(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed,
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

private:
    IWiFi& wifi_;
    IPreferences& prefs_;
    IStatusLED& statusLed_;
    const char* bakedSsid_;
    const char* bakedPass_;

    WiFiState::Context ctx_;
    NtpInitCallback ntpInitCallback_;
    TcpServerRestartCallback tcpServerRestartCallback_;

    // State handlers
    std::unique_ptr<IWiFiStateHandler> disconnectedHandler_;
    std::unique_ptr<IWiFiStateHandler> connectingHandler_;
    std::unique_ptr<IWiFiStateHandler> reconnectingHandler_;
    std::unique_ptr<IWiFiStateHandler> connectedStaHandler_;
    std::unique_ptr<IWiFiStateHandler> connectedApHandler_;

    void applyStateTransition(const StateTransition& transition);
    IWiFiStateHandler* getStateHandler(WiFiState::State state);
};

// Testable pure functions - standalone in namespace for testability
CredentialSource determineCredentialSource(IPreferences& prefs, const char* bakedSsid, const char* bakedPass);
bool shouldFallbackToApMode(CredentialSource source, uint32_t connectDurationMs);
bool isInitialConnectTimeout(uint32_t connectDurationMs);
bool shouldRetryWiFi(WiFiState::State state, uint32_t now, uint32_t lastRetry);
bool loadCredentialsImpl(IPreferences& prefs, std::string& ssid, std::string& pass);

} // namespace esp32_firmware