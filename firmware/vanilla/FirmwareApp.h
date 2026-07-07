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

namespace esp32_firmware {

// Forward declarations (avoid circular dependencies)
class CanBridge;
class AtCommandDispatcher;
class OtaUpdateServer;

// Re-use interfaces from WiFiManager.h (IWiFi, IPreferences, IStatusLED)

// Callbacks for firmware-side effects (bridge to .ino)
struct FirmwareCallbacks {
    // TCP server should restart (WiFi reconnected with new IP)
    std::function<void()> restartTcpServer;

    // Discovery packet should broadcast
    std::function<void()> broadcastDiscovery;

    // OTA update handler
    std::function<void()> handleOta;
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
    // - deviceId: 16-byte device ID for discovery packets
    // - bakedSsid/bakedPass: optional compile-time WiFi credentials
    FirmwareApp(IWiFi& wifi, IPreferences& prefs, IStatusLED& statusLed,
                IWiFiDiscovery& wifiDiscovery, IUdp& udp, ITime& time,
                const std::array<uint8_t, 16>& deviceId,
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

private:
    // Dependencies
    IWiFi& wifi_;
    IPreferences& prefs_;
    IStatusLED& statusLed_;
    IWiFiDiscovery& wifiDiscovery_;
    IUdp& udp_;
    ITime& time_;
    const std::array<uint8_t, 16>& deviceId_;
    const char* bakedSsid_;
    const char* bakedPass_;

    // Managers (owned by this app)
    std::unique_ptr<WiFiManager> wifiManager_;
    std::unique_ptr<DiscoveryManager> discoveryManager_;
    std::unique_ptr<CanBridge> canBridge_;
    std::unique_ptr<AtCommandDispatcher> atDispatcher_;
    std::unique_ptr<OtaUpdateServer> otaServer_;

    // Callbacks for firmware-side effects
    FirmwareCallbacks callbacks_;

    // Initialization state
    bool initialized_;

    // Discovery UDP socket is opened on the first update() tick (not during init()),
    // so the hardware-touching udp_.begin() never runs on the boot path before the
    // WiFi netif is up. Set true once DiscoveryManager::init() has run.
    bool discoveryStarted_;

    // Helper methods
    void setupManagers();
    void setupCallbacks();
};

} // namespace esp32_firmware
