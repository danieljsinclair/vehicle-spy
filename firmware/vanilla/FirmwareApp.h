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
class FirmwareApp {
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
    // - bakedSsid/bakedPass: optional compile-time WiFi credentials
    FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed,
                IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                ISntp& sntp, ITimeNtp& timeNtp,
                const std::array<uint8_t, 16>& deviceId,
                const CanBridgeDeps& canBridgeDeps,
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

    // Get current WiFi state (for debugging/testing)
    int getWiFiState() const;

    // Check if TCP server needs restart (after WiFi reconnect)
    bool shouldRestartTcpServer() const;
    void clearTcpServerRestartFlag();

    // Credential operations (for AT commands)
    bool clearCredentials();
    bool loadCredentials(std::string& ssid, std::string& pass) const;

    // CAN bridge: the .ino routes monitor-state and the TWAI RX drain here so
    // frame streaming (Serial always, TCP when connected+monitoring) runs through
    // the vanilla CanBridge instead of inline logic.
    void setMonitorActive(bool active);
    bool isMonitorActive() const;
    void processCanFrames(uint32_t serialQuietUntilMs);

    // Discovery (Stage 3 of the .ino → vanilla extraction): the .ino owns no
    // discovery logic itself — FirmwareApp drives the vanilla DiscoveryManager.
    // The .ino only injects the build-time feature toggle and the live TCP-client
    // state, and resets the backoff timer on boot / buddy-disconnect. The actual
    // UDP socket open + broadcast cadence live inside DiscoveryManager (which
    // performs the send via the injected ArduinoUdp adapter); the broadcast
    // callback remains a post-send firmware-effect hook (e.g. LED pulse).
    void setDiscoveryEnabled(bool enabled);
    void setClientConnected(bool connected);
    void resetDiscoveryBackoff();

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
    void handleTcpAtCommand(const std::string& cmd);
    void handleSerialAtCommand(const std::string& cmd);

private:
    // Dependencies
    IWiFi& wifi_;
    IPreferences& prefs_;
    IStatusLED& statusLed_;
    IWiFiDiscovery& wifiDiscovery_;
    IUdp& udp_;
    ITime& time_;
    const std::array<uint8_t, 16>& deviceId_;
    CanBridgeDeps canBridgeDeps_;
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
    bool discoveryStarted_;

    // NTP sync is deferred until WiFi is connected (no socket/hardware work at
    // boot — mirrors the DiscoveryManager deferral). Set true once NtpTimeSync has
    // been told to start; the WiFiManager NTP-init callback is the trigger.
    bool ntpStarted_;

    // Discovery control flags injected from the .ino (build toggle + live client
    // state). Defaults (enabled / no client) match the prior hardcoded-inline
    // behavior so existing tests and the default build stay green.
    bool discoveryEnabled_ = true;
    bool clientConnected_ = false;

    // Helper methods
    // constructManagers() builds the owned manager objects from the PASSED-ONLY
    // interface refs and is called from the ctor (where those refs are in scope).
    // It performs construction ONLY — no hardware/netif work — so it is safe at
    // static-init time. The hardware-touching init() calls are deferred to init().
    void constructManagers(ISntp& sntp, ITimeNtp& timeNtp);
    void setupCallbacks();
};

} // namespace esp32_firmware
