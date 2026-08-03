#pragma once

// FirmwareApp.h - Thin-veneer orchestration layer for ESP32 firmware
// Extracted from can-bridge.ino for host testability
//
// This class owns all vanilla manager components and provides the main loop
// orchestration. The .ino becomes pure construction + dispatch.
//
// Design Principles:
// - SOLID: SRP (orchestration only), DI (all dependencies injected), OCP (Strategy pattern)
// - TDD: All logic testable via mocks; .ino is untested glue
// - Fail-fast: Assertions on invariants; defensive only at external boundaries

#include <cstdint>
#include <functional>
#include <memory>
#include <array>
#include "WiFiManager.h"
#include "DiscoveryManager.h"
#include "NtpTimeSync.h"
#include "CanBridge.h"
#include "AtCommandDispatcher.h"  // owns AtCommandDispatcher + the ITcpClientAt/ISerialAt/IEspAt/IWifiCredentialStore/IMonitorState AT boundaries
#include "ITcpServer.h"           // ITcpHostCallbacks (TcpServerManager delegation seam)
#include "IClientConnectionSource.h" // IClientConnectionSource (client connection seam)
#include "FactoryResetCheck.h"    // ICredentialClear (factory-reset credential boundary)
#include "ISerialEventLogger.h"   // IEventLogger (centralized observability)

namespace esp32_firmware {

// Forward declarations (avoid circular dependencies)
class CanBridge;
struct CanBridgeDeps;
class AtCommandDispatcher;

// Re-use interfaces from WiFiManager.h (IWiFi, IPreferences, IStatusLED)

// Callbacks for firmware-side effects (bridge to .ino)
struct FirmwareCallbacks {
    // TCP server should restart (WiFi reconnected with new IP)
    std::function<void()> restartTcpServer;

    // Discovery packet should broadcast
    std::function<void()> broadcastDiscovery;
};

// FirmwareApp - Main application orchestrator
//
// Responsibilities:
// - Own all vanilla manager instances
// - Coordinate managers in the update loop
// - Bridge manager callbacks to firmware effects
// - Provide factory-reset and credential operations
//
// Thread safety: Single-threaded (ESP32 Arduino main loop)
//
// FirmwareApp is the orchestrator, so it directly implements the narrow
// interfaces its owned managers consume (ISP/SRP): this removes the prior
// "wrapper-of-wrapper" forwarder adapter structs that lived in the .ino purely
// to bounce a call straight back into FirmwareApp.
//   - ITcpHostCallbacks : TcpServerManager delegation (cmd dispatch, monitor,
//                        discovery backoff, WiFi-state read)
//   - IMonitorState     : AtCommandDispatcher monitor-flag boundary
//   - ICredentialClear  : FactoryResetCheck credential-wipe boundary
class FirmwareApp : public ITcpHostCallbacks,
                    public IMonitorState,
                    public ICredentialClear {
public:
    // Constructor - inject dependencies
    // - wifi: WiFi interface (ArduinoWiFi or mock)
    // - prefs: preferences interface (ArduinoPreferences or mock)
    // - statusLed: LED interface (StatusLED or mock)
    // - wifiDiscovery: WiFi discovery interface (ArduinoWiFi also implements this, or mock)
    // - udp: UDP interface for discovery (ArduinoUdp or mock)
    // - time: time interface for discovery (ArduinoTime or mock)
    // - sntp: SNTP interface for NTP time sync (ArduinoSntp or mock)
    // - timeNtp: time interface for NTP sync (ArduinoTimeNtp or mock)
    // - deviceId: 16-byte device ID for discovery packets
    // - canBridgeDeps: adapters wiring CanBridge to hardware (CAN/TCP/Serial)
    // - clientConnectionSource: queries whether a TCP client is adopted by
    //   TcpServerManager (eliminates the global-WiFiClient desync that fed
    //   selectLedPattern with stale connection state)
    // - bakedSsid/bakedPass: optional compile-time WiFi credentials
    FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed,
                IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                ISntp& sntp, ITimeNtp& timeNtp,
                const std::array<uint8_t, 16>& deviceId,
                const CanBridgeDeps& canBridgeDeps,
                IClientConnectionSource& clientConnectionSource,
                const char* bakedSsid = nullptr, const char* bakedPass = nullptr);

    ~FirmwareApp();

    // Initialize the application (call from setup())
    //
    // Pre-conditions:
    // - Serial is initialized (for debug output)
    // - WiFi is ready (for event registration)
    //
    // Post-conditions:
    // - All managers are initialized
    // - WiFi state machine is running
    void init();

    // Main update loop (call from loop())
    //
    // Parameters:
    // - now: Current millis() timestamp
    //
    // Side effects:
    // - Updates WiFi state machine
    // - Updates LED patterns
    // - Triggers callbacks (TCP restart, discovery broadcast, OTA)
    void update(uint32_t now);

    // WiFi event callback (call from Arduino WiFi event handler)
    void onWiFiDisconnected(int reason);

    // Factory reset - clear stored WiFi credentials
    // Returns true if credentials were cleared
    bool factoryReset();

    // Store WiFi credentials to NVS
    bool storeCredentials(const std::string& ssid, const std::string& pass);

    // Check if stored credentials exist
    bool hasStoredCredentials() const;

    // Set firmware-side callbacks
    void setCallbacks(const FirmwareCallbacks& callbacks);

    // Set the centralized event logger (injected once, used for all [EVENT] and
    // [STATE] emissions). The .ino supplies a SerialEventLogger; tests inject a mock.
    void setEventLogger(IEventLogger& logger);

    // Get current WiFi state (for debugging/testing)
    // ITcpHostCallbacks: read by TcpServerManager for the LED-revert-on-disconnect
    // decision (only revert to WIFI_CONNECTED when WiFi is WIFI_CONNECTED/WIFI_AP_MODE).
    int getWiFiState() const override;

    // ── ITcpHostCallbacks: serial observability callbacks ──────────────────────
    // Called by TcpServerManager at client lifecycle transitions. FirmwareApp
    // routes these to its owned IEventLogger (centralized observability).
    void onClientConnected(const std::string& ip) override;
    void onAuthFailed(const std::string& ip) override;
    void onClientDisconnected(const std::string& ip, int reason) override;

    // ── Observability getters (called by .ino for LoopHeartbeat enrichment) ─────
    // Remote IP of the currently-adopted TCP client, or empty string when none.
    std::string getClientIp() const { return observability_.clientIp; }

    // Current discovery broadcast cadence as a human string (e.g. "500ms", "10s").
    // Computed from DiscoveryManager's backoff state + the supplied nowMs.
    std::string getDiscoveryCadence(uint32_t nowMs) const;

    // Last LED pattern set by FirmwareApp::update() (mirrors selectLedPattern).
    int getCurrentLedPattern() const { return observability_.lastLedPattern; }

    // Check if TCP server needs restart (after WiFi reconnect)
    bool shouldRestartTcpServer() const;
    void clearTcpServerRestartFlag();

    // Credential operations (for AT commands)
    bool clearCredentials();
    bool loadCredentials(std::string& ssid, std::string& pass) const;

    // ICredentialClear: FactoryResetCheck wipes stored WiFi credentials through
    // this narrow seam. Delegates to clearCredentials() (the bool return —
    // whether NVS had credentials to clear — is not needed by the reset flow,
    // which only requires the wipe to be performed). Behavior-identical to the
    // prior ArduinoCredClear forwarder that lived in the .ino.
    void clear() override;

    // CAN bridge: the .ino routes monitor-state and the TWAI RX drain here so
    // frame streaming (Serial always, TCP when connected+monitoring) runs through
    // the vanilla CanBridge instead of inline logic.
    // IMonitorState (AtCommandDispatcher) + ITcpHostCallbacks (TcpServerManager)
    // both reach the monitor flag through this single override.
    void setMonitorActive(bool active) override;
    bool isMonitorActive() const;
    // nowMs is the caller's current tick (millis()). It is forwarded to
    // CanBridge so the serial quiet-window can be evaluated as a DEADLINE
    // comparison rather than a stuck boolean — the clock is injected by the
    // caller (same idiom as update(now) / TcpServerManager::cycle(nowMs))
    // instead of being read from a hidden global.
    void processCanFrames(uint32_t nowMs);
    // Serial quiet-window ownership moved out of the .ino global (cpp:S5421):
    // the .ino sets this (millis()-based) when it drains a serial AT command;
    // processCanFrames() forwards it to CanBridge::processFrames so serial
    // emission is suppressed until that deadline passes.
    void setSerialQuietUntilMs(uint32_t ms);

    // Discovery (Stage 3 of the .ino → vanilla extraction): the .ino owns no
    // discovery logic itself — FirmwareApp drives the vanilla DiscoveryManager.
    // The .ino only injects the build-time feature toggle and the live TCP-client
    // state, and resets the backoff timer on boot / buddy-disconnect. The actual
    // UDP socket open + broadcast cadence live inside DiscoveryManager (which
    // performs the send via the injected ArduinoUdp adapter); the broadcast
    // callback remains a post-send firmware-effect hook (e.g. LED pulse).
    void setDiscoveryEnabled(bool enabled);
    // ITcpHostCallbacks: TcpServerManager resets the discovery backoff timer on
    // buddy-disconnect so a new buddy is welcomed promptly.
    void resetDiscoveryBackoff() override;

    // AT command handling: the .ino constructs the five runtime-boundary adapters
    // over Arduino (WiFiClient/Serial/ESP/Preferences) and hands them in here. We
    // own a single AtCommandDispatcher and route both the TCP and serial command
    // reads through it, replacing the .ino's inline handler structs + dispatch
    // loop (Stage 2 of the .ino → vanilla extraction).
    //
    // deviceId is read by REFERENCE and must already be populated (the .ino fills
    // discoveryDeviceId from the MAC in setup() AFTER the static FirmwareApp
    // construction). The dispatcher reads it the first time a command is handled
    // (lazy construction), so it always sees the live, populated array.
    void setAtCommandAdapters(ITcpClientAt& tcpClient, ISerialAt& serial, IEspAt& esp,
                              IWifiCredentialStore& wifiStore, IMonitorState& monitor,
                              const std::array<uint8_t, 16>& deviceId);
    // ITcpHostCallbacks: TcpServerManager forwards each received AUTH'd command
    // line here (frames the reply as "<resp>\r\r>" for the host HELO handshake).
    void handleTcpAtCommand(const std::string& cmd) override;
    void handleSerialAtCommand(const std::string& cmd);

private:
    // Dependencies
    IWiFi& wifi_;
    IStatusLED& statusLed_;
    CanBridgeDeps canBridgeDeps_;
    IClientConnectionSource& clientConnectionSource_;
    const char* bakedSsid_;
    const char* bakedPass_;

    // Managers (owned by this app)
    std::unique_ptr<WiFiManager> wifiManager_;
    std::unique_ptr<DiscoveryManager> discoveryManager_;
    std::unique_ptr<NtpTimeSync> ntpTimeSync_;
    std::unique_ptr<CanBridge> canBridge_;
    std::unique_ptr<AtCommandDispatcher> atDispatcher_;

    // Callbacks for firmware-side effects
    FirmwareCallbacks callbacks_;

    // Initialization state
    bool initialized_;

    // Discovery UDP socket is opened on the first update() tick (not during init()),
    // so the hardware-touching udp_.begin() never runs on the boot path before the
    // WiFi netif is up. Set true once DiscoveryManager::init() has run.

    // NTP sync is deferred until WiFi is connected (no socket/hardware work at
    // boot — mirrors the DiscoveryManager deferral). Set true once NtpTimeSync has
    // been told to start; the WiFiManager NTP-init callback is the trigger.
    bool ntpStarted_;

    // Discovery control flags injected from the .ino (build toggle + live client
    // state). Defaults (enabled / no client) match the prior hardcoded-inline
    // behavior so existing tests and the default build stay green.

    // Last interval at which we emitted a discovery_broadcast [EVENT]. Used to
    // throttle the event to once per cadence-tier change rather than every
    // broadcast (which floods at the 500ms rapid tier).

    // Serial quiet-window (millis()-based ms). Set from the .ino via
    // setSerialQuietUntilMs(); read by processCanFrames(). Promoted from a
    // mutable .ino global to clear cpp:S5421 (global variables should be const).
    uint32_t serialQuietUntilMs_ = 0;

    // ── Serial observability state ────────────────────────────────────────────
    // Centralized event logger (set via setEventLogger before the first update()).

    // Remote IP of the currently-adopted TCP client (set by TcpServerManager
    // callbacks; read by LoopHeartbeat via getClientIp()).

    // Last LED pattern value passed to statusLed_.setPattern() (mirrors the
    // selectLedPattern result so getCurrentLedPattern() can expose it).

    // Previous WiFi state for transition detection in update(). Initialized to
    // WIFI_DISCONNECTED so the first real connected transition fires wifi_connected.
    int previousWifiState_ = static_cast<int>(WiFiState::State::WIFI_DISCONNECTED);

    // ── Bundled state (cpp:S1820: keep field count under 20) ──────────────────
    struct DiscoveryState {
        bool started = false;
        bool enabled = true;
        uint32_t lastBroadcastEventIntervalMs = 0;
    };
    struct ObservabilityState {
        IEventLogger* logger = nullptr;
        std::string clientIp;
        int lastLedPattern = 0;
        int lastDisconnectReason = 0;
    };
    DiscoveryState discovery_;
    ObservabilityState observability_;

    // Helper methods
    // constructManagers() builds the owned manager objects from the PASSED-ONLY
    // interface refs and is called from the ctor (where those refs are in scope).
    // It performs construction ONLY — no hardware/netif work — so it is safe at
    // static-init time. The hardware-touching init() calls are deferred to init().
    void constructManagers(const std::array<uint8_t, 16>& deviceId, IPreferences& prefs,
                           IUdp& udp, IWiFiDiscovery& wifiDiscovery, ITime& time,
                           ISntp& sntp, ITimeNtp& timeNtp);
    void setupCallbacks();

    // Emit a single [EVENT] line through the centralized logger (no-op if no
    // logger is injected). detail is the pre-formatted key=value payload.
    void emitEvent(const char* eventType, const std::string& detail);
};

} // namespace esp32_firmware
