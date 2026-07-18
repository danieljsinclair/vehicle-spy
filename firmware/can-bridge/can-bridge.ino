// ESP32 CAN-to-WiFi TCP Bridge
// Streams vehicle CAN frames over WiFi to vehicle-sim CLI
//
// Hardware: ESP32-WROOM-32 + SN65HVD230 CAN transceiver
// Wiring:   GPIO 22 → transceiver TX, GPIO 21 → transceiver RX
//           Transceiver CANH → OBD2 pin 6, CANL → OBD2 pin 14
//
// WiFi:     Station mode if ESP32_WIFI_SSID/ESP32_WIFI_PASS defined at build time
//           Falls back to AP mode (ESP32-CAN / cancan12) if not set
// TCP:      port 3333 (CAN bridge)
// OTA:      port 80 (HTTPUpdateServer — standard Arduino OTA)
// Protocol: Minimal ELM327 — ATZ, ATE0, ATSP6, ATH1, ATMA

#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <sntp.h>
#include <Preferences.h>  // NVS storage for WiFi credentials
#include <driver/twai.h>  // TWAI for CAN communication
#include <vector>         // Command pattern registry
#include <array>          // Fixed-size buffers (S5945)
#include <algorithm>      // std::copy for byte buffers
#include <type_traits>    // static_assert noexcept-move checks (S5018)
#include <utility>        // std::move for noexcept move ops (S5018)

// DEFERRED: this .ino accumulates WiFi/AT/discovery/OTA/StatusLED handlers in one translation unit (SRP). Extract to separate .cpp units when adding the next handler.

// ── StatusLED Class Definitions ─────────────────────────────────────────────────────
// Declarative pattern-based LED implementation with SOLID principles and TDD
#include "StatusLED.h"
#include "HardwareStatusLEDOutput.h"

// ── FirmwareApp Components ───────────────────────────────────────────────────────────
// Vanilla firmware orchestrator (WiFi/LED state machine + callback seams)
#include "ArduinoWiFi.h"
#include "ArduinoPreferences.h"
#include "ArduinoUdp.h"
#include "ArduinoTime.h"
#include "ArduinoSntp.h"
#include "ArduinoTimeNtp.h"
#include "FirmwareApp.h"

// ── TcpServerManager (Stage 6 extraction) ────────────────────────────────────────────
// Vanilla accept/auth/dispatch state machine (host-tested, 14 tests). The .ino
// supplies the WiFiServer/WiFiClient adapters + a narrow ITcpHostCallbacks impl
// backed by firmwareApp. Inline TCP loop deleted (was loop() L634-696).
#include "ITcpServer.h"
#include "TcpServerManager.h"
#include "ArduinoTcpServer.h"

// Use firmware namespace for components
using firmware::StatusLED;
using firmware::HardwareStatusLEDOutput;
using esp32_firmware::ArduinoWiFi;
using esp32_firmware::ArduinoPreferences;
using esp32_firmware::ArduinoUdp;
using esp32_firmware::ArduinoTime;
using esp32_firmware::ArduinoSntp;
using esp32_firmware::ArduinoTimeNtp;
using esp32_firmware::FirmwareApp;
using esp32_firmware::TcpServerManager;
using esp32_firmware::ArduinoTcpServer;
using esp32_firmware::ITcpHostCallbacks;

// ── Named Constants (no magic numbers) ──────────────────────────────────────
namespace Constants {
    // Network ports
    static constexpr uint32_t TCP_PORT = 3333;
    static constexpr uint16_t DISCOVERY_PORT = 3335;
    static constexpr uint16_t OTA_HTTP_PORT = 80;

    // Timing intervals (milliseconds)
    static constexpr uint32_t WIFI_CONNECT_RETRY_INTERVAL_MS = 5000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t WIFI_INITIAL_CONNECT_MAX_RETRIES = 60;  // 5 minutes at 5s interval
    static constexpr uint32_t SERIAL_BAUD = 115200;
    static constexpr uint32_t SERIAL_QUIET_DURATION_MS = 250;

    // TCP timeouts
    static constexpr uint32_t TCP_AUTH_TIMEOUT_MS = 5000;
    static constexpr uint32_t TCP_COMMAND_TIMEOUT_MS = 100;
    static constexpr uint32_t TCP_REBOOT_DELAY_MS = 100;

    // Buffer sizes
    static constexpr size_t MAX_SERIAL_CMD_LENGTH = 64;
    static constexpr size_t DISCOVERY_PACKET_SIZE = 106;  // 42 header + 64 signature
    static constexpr size_t CAN_FRAME_BUFFER_SIZE = 32;

    // Hardware pins
    static constexpr gpio_num_t TWAI_TX = GPIO_NUM_22;
    static constexpr gpio_num_t TWAI_RX = GPIO_NUM_21;

    // NTP (Network Time Protocol) settings
    static constexpr int32_t NTP_RETRY_INTERVAL_MS = 15000;
    static constexpr uint32_t NTP_SYNC_RETRY_MAX = 3;

    // Discovery backoff schedule (milliseconds)
    static constexpr uint32_t DISCOVERY_INTERVAL_FAST_MS = 500;
    static constexpr uint32_t DISCOVERY_INTERVAL_1_5_MIN_MS = 3000;
    static constexpr uint32_t DISCOVERY_INTERVAL_5_10_MIN_MS = 6000;
    static constexpr uint32_t DISCOVERY_INTERVAL_10_15_MIN_MS = 10000;
    static constexpr uint32_t DISCOVERY_INTERVAL_15_30_MIN_MS = 30000;
    static constexpr uint32_t DISCOVERY_INTERVAL_SLOW_MS = 60000;

    // Discovery backoff time thresholds (milliseconds)
    static constexpr uint32_t DISCOVERY_AGE_1_MIN_MS = 60000;
    static constexpr uint32_t DISCOVERY_AGE_5_MIN_MS = 300000;
    static constexpr uint32_t DISCOVERY_AGE_10_MIN_MS = 600000;
    static constexpr uint32_t DISCOVERY_AGE_15_MIN_MS = 900000;
    static constexpr uint32_t DISCOVERY_AGE_30_MIN_MS = 1800000;

    // Firmware info
    static constexpr const char* FIRMWARE_VERSION = "0.2.0";
    static constexpr const char* DEVICE_NAME = "ESP32-CAN-Bridge";

    // Factory reset pin hook (future implementation)
    // GPIO pin for factory reset: hold during boot → wipe stored WiFi NVS → boot to AP mode
    // Same firmware, no reflash needed. Pin pull-up recommended; active-low logic.
    // MARKER: FACTORY_RESET_HOOK (future implementation, not an open task)
    static constexpr gpio_num_t FACTORY_RESET_PIN = GPIO_NUM_0;  // Boot button (GPIO0)
    static constexpr uint32_t FACTORY_RESET_HOLD_MS = 3000;      // Hold 3 seconds to trigger
}

// AT command handling was extracted from this .ino into the vanilla
// AtCommandDispatcher (firmware/vanilla/AtCommandDispatcher.{h,cpp}). The .ino
// no longer owns the command structs (AtCommandResult/SetWifiParams/
// AtCommandHandler), handler registry, or dispatch loop — it delegates both the
// TCP and serial command reads to a single AtCommandDispatcher owned by
// FirmwareApp. The vanilla types are the canonical ones; the inline S5018
// noexcept-move edits on the old inline structs are superseded by this
// extraction (see Stage 2 notes).

// ANSI color codes for serial output
static const char* const RED    = "\033[0;31m";
static const char* const GREEN  = "\033[0;32m";
static const char* const BLUE   = "\033[0;34m";
static const char* const CYAN   = "\033[0;36m";
static const char* const PURPLE = "\033[0;35m";
static const char* const YELLOW  = "\033[0;33m";
static const char* const NC     = "\033[0m";

// ── NVS Storage (WiFi Credentials) ────────────────────────────────────────────
// Only the WRITE path remains inline (used by ArduinoAtWifiStore::store, the AT
// command NVS-write adapter). READ / has / clear paths are owned by WiFiManager
// via the injected IPreferences (ArduinoPreferences) — FirmwareApp exposes
// hasStoredCredentials()/loadCredentials()/clearCredentials() for them.
static Preferences wifiCredentials;  // NVS storage for WiFi SSID/password

// NVS keys for WiFi credentials storage
static constexpr const char* NVS_WIFI_NAMESPACE = "wifi";
static constexpr const char* NVS_WIFI_SSID = "ssid";
static constexpr const char* NVS_WIFI_PASS = "pass";

// Store WiFi credentials to NVS (ATSETWIFI command path)
static bool storeWifiCredentials(const String& ssid, const String& pass) {
    wifiCredentials.begin(NVS_WIFI_NAMESPACE, false);  // Read-write
    bool success = wifiCredentials.putString(NVS_WIFI_SSID, ssid) > 0;
    success = success && wifiCredentials.putString(NVS_WIFI_PASS, pass) > 0;
    wifiCredentials.end();
    return success;
}

// Device ID declaration - needed for message tagging
static std::array<uint8_t, 16> discoveryDeviceId;

// ── Message Tagging ────────────────────────────────────────────────────────────
// Tag Serial diagnostic messages with device ID for clarity in monitor output
static String deviceMessageTag() {
    std::array<char, 32> tag{};
    snprintf(tag.data(), tag.size(), "[%02X%02X%02X%02X] ",
             discoveryDeviceId[0], discoveryDeviceId[1],
             discoveryDeviceId[2], discoveryDeviceId[3]);
    return String(tag.data());
}

// Helper to print tagged messages (optional - can be used for key diagnostics)
static void printTagged(const char* color, const char* message) {
    Serial.printf("%s%s%s: %s%s\r\n", color, deviceMessageTag().c_str(), message, NC, color);
}

// TCP auth token — injected at build time, never stored on disk
#ifndef TCP_AUTH_TOKEN
#define TCP_AUTH_TOKEN "vehicle-sim-2026"
#endif

// Discovery signing (optional): Ed25519 private key for signing discovery packets
// If enabled, discovery packets are signed so the host can verify device authenticity.
// Key format: 32-byte Ed25519 private seed (RFC 8032). Bake in via build flags.
// NOTE: This is OPTIONAL. If not defined, discovery packets are unsigned (zeros).
#ifndef VEHICLE_SIM_DISCOVERY_SIGNING_KEY
#define VEHICLE_SIM_DISCOVERY_SIGNING_KEY
#endif

// Guarded discovery signing - only available if libsodium is linked
#ifndef VEHICLE_SIM_ENABLE_DISCOVERY_SIGNING
#define VEHICLE_SIM_ENABLE_DISCOVERY_SIGNING 0
#endif

// Color constants declared earlier

#ifndef VEHICLE_SIM_ENABLE_OTA_SERVER
#define VEHICLE_SIM_ENABLE_OTA_SERVER 1
#endif

#ifndef VEHICLE_SIM_ENABLE_DISCOVERY
#define VEHICLE_SIM_ENABLE_DISCOVERY 1
#endif

#ifndef VEHICLE_SIM_ENABLE_TWAI
#define VEHICLE_SIM_ENABLE_TWAI 1
#endif

// OTA update entry points (implemented in ota_update.ino, part of this sketch).
//   otaMarkValidOnBoot - mark the running app valid to cancel an OTA rollback
//   otaSetup           - start the HTTPUpdateServer /update endpoint on port 80
//   otaLoop            - service incoming OTA connections each loop tick
#if VEHICLE_SIM_ENABLE_OTA_SERVER
void otaMarkValidOnBoot();
void otaSetup();
void otaLoop();
#endif

// WiFi credentials injected via compiler defines (never stored on disk)
// Build with: make flash ESP32_WIFI_SSID=X ESP32_WIFI_PASS=Y
#ifdef ESP32_WIFI_SSID
#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)
static constexpr const char* WIFI_SSID = STRINGIZE(ESP32_WIFI_SSID);
static constexpr const char* WIFI_PASSWORD = STRINGIZE(ESP32_WIFI_PASS);
#else
static constexpr const char* WIFI_SSID = nullptr;
static constexpr const char* WIFI_PASSWORD = nullptr;
#endif

static constexpr const char* AP_SSID = "ESP32-CAN";
static constexpr const char* AP_PASS = "cancan12";

// ── Status LED ─────────────────────────────────────────────────────────────────
// Visual feedback using the blue LED on GPIO2
static HardwareStatusLEDOutput ledOutput(2);  // GPIO2 for ESP32 blue LED
static StatusLED statusLed(&ledOutput);

// ── FirmwareApp Components ─────────────────────────────────────────────────────
// Arduino adapters for vanilla interfaces (scoped to .ino via ARDUINO ifdef)
static ArduinoWiFi arduinoWiFi;
static ArduinoPreferences arduinoPrefs;
static ArduinoUdp arduinoUdp;
static ArduinoTime arduinoTime;
static ArduinoSntp arduinoSntp;        // ISntp adapter for NTP sync
static ArduinoTimeNtp arduinoTimeNtp;  // ITimeNtp adapter for NTP sync

// Baked credentials: use the build-injected ESP32_WIFI_SSID/ESP32_WIFI_PASS when
// present (the real station credentials), otherwise nullptr so WiFiManager falls
// back to AP mode. NOTE: the previous refactor hardcoded dummy "baked-ssid"/
// "baked-pass" here, which made FirmwareApp call WiFi.begin() with bogus creds and
// never join the real network. Must match the inline state machine's WIFI_SSID.
static constexpr const char* BAKED_SSID = (WIFI_SSID != nullptr) ? WIFI_SSID : nullptr;
static constexpr const char* BAKED_PASS = (WIFI_PASSWORD != nullptr) ? WIFI_PASSWORD : nullptr;

// ── CAN Bridge Arduino Adapters ──────────────────────────────────────────
// Thin adapters implementing CanBridge's vanilla interfaces over the ESP32
// hardware. Construction/install of the TWAI driver happens once in setup()
// (hardware-touching), so the adapter only READS frames here (safe post-boot).
// NOTE: the connected-buddy WiFiClient must be declared before these adapters
// because their member bodies reference it (complete-class context).
static WiFiClient client;
#if VEHICLE_SIM_ENABLE_TWAI
struct ArduinoCanDriver : public esp32_firmware::ICanDriver {
    int driverInstall(esp32_firmware::CanGeneralConfig*, esp32_firmware::CanTimingConfig*, esp32_firmware::CanFilterConfig*) override { return 0; }  // done in setup()
    int start() override { return 0; }                              // done in setup()
    int receive(esp32_firmware::CanFrame* msg, uint32_t timeoutMs) override {
        twai_message_t m{};
        if (twai_receive(&m, timeoutMs) != ESP_OK) return -1;
        msg->identifier = m.identifier;
        msg->data_length_code = m.data_length_code;
        std::copy(std::begin(m.data), std::end(m.data), std::begin(msg->data));
        return 0;  // ESP_OK
    }
};
#else
struct ArduinoCanDriver : public esp32_firmware::ICanDriver {
    int driverInstall(esp32_firmware::CanGeneralConfig*, esp32_firmware::CanTimingConfig*, esp32_firmware::CanFilterConfig*) override { return -1; }
    int start() override { return -1; }
    int receive(esp32_firmware::CanFrame*, uint32_t) override { return -1; }
};
#endif

// ITcpClient wrapping the global WiFiClient (the connected buddy).
struct ArduinoTcpClient : public esp32_firmware::ITcpClient {
    bool connected() const override { return client && client.connected(); }
    size_t print(const char* str) override { return client.print(str); }
    void flush() override { client.flush(); }
};

// ISerialCan wrapping Serial (USB diagnostic logging).
struct ArduinoSerialCan : public esp32_firmware::ISerialCan {
    size_t print(const char* str) override { return Serial.print(str); }
    void flush() override { Serial.flush(); }
};

static ArduinoCanDriver arduinoCanDriver;
static ArduinoTcpClient arduinoTcpClient;
static ArduinoSerialCan arduinoSerialCan;
static esp32_firmware::CanBridgeDeps canBridgeDeps{
    arduinoCanDriver, arduinoTcpClient, arduinoSerialCan
};

// ── AT Command Adapters (vanilla AtCommandDispatcher boundaries) ────────────
// Thin Arduino implementations of the vanilla AT-boundary interfaces. The .ino
// owns the hardware objects (WiFiClient/Serial/ESP/Preferences); FirmwareApp owns
// the AtCommandDispatcher and is handed these adapters so the device-specific
// I/O stays in the .ino while the command logic lives in vanilla code.
// Forward-declare FirmwareApp so adapter methods can reference it (static init order).
class FirmwareApp;
extern FirmwareApp firmwareApp;
struct ArduinoAtTcpClient : public esp32_firmware::ITcpClientAt {
    void print(const char* str) override { client.print(str); }
    void flush() override { client.flush(); }
};

struct ArduinoAtSerial : public esp32_firmware::ISerialAt {
    void println(const char* str) override { Serial.println(str); }
    void flush() override { Serial.flush(); }
};

struct ArduinoAtEsp : public esp32_firmware::IEspAt {
    void restart() override {
        delay(Constants::TCP_REBOOT_DELAY_MS);
        ESP.restart();
    }
};

struct ArduinoAtWifiStore : public esp32_firmware::IWifiCredentialStore {
    bool store(const std::string& ssid, const std::string& password) override {
        // Bridge vanilla std::string -> Arduino String for the NVS write.
        return storeWifiCredentials(String(ssid.c_str()), String(password.c_str()));
    }
};

struct ArduinoAtMonitor : public esp32_firmware::IMonitorState {
    void setMonitorActive(bool active) override { firmwareApp.setMonitorActive(active); }
};

static ArduinoAtTcpClient arduinoAtTcpClient;
static ArduinoAtSerial arduinoAtSerial;
static ArduinoAtEsp arduinoAtEsp;
static ArduinoAtWifiStore arduinoAtWifiStore;
static ArduinoAtMonitor arduinoAtMonitor;

// FirmwareApp orchestrator - delegates WiFi/LED/NTP/CAN to vanilla managers
// ArduinoWiFi implements both IWiFi and IWiFiDiscovery
// NTP is routed through FirmwareApp (owns NtpTimeSync + ArduinoSntp/ArduinoTimeNtp)
// CanBridge is constructed inside FirmwareApp from the adapter bundle above.
FirmwareApp firmwareApp(arduinoWiFi, arduinoPrefs, statusLed,
                              arduinoWiFi, arduinoUdp, arduinoTime,
                              arduinoSntp, arduinoTimeNtp,
                              discoveryDeviceId,
                              canBridgeDeps,
                              BAKED_SSID, BAKED_PASS);

static WiFiServer tcpServer(Constants::TCP_PORT);
static uint32_t serialQuietUntilMs = 0;

// ── TCP Server Manager wiring (Stage 6) ──────────────────────────────────────────────
// ArduinoTcpServer adapts the global tcpServer + client (the single connection
// truth source); TcpServerManager drives the accept/auth/dispatch lifecycle.
// The ITcpHostCallbacks impl below delegates the 4 out-of-SRP behaviours to
// firmwareApp (command dispatch, monitor flag, discovery backoff, WiFi state).
namespace {
struct FirmwareAppTcpHostCallbacks : public ITcpHostCallbacks {
    FirmwareApp& app;
    explicit FirmwareAppTcpHostCallbacks(FirmwareApp& a) : app(a) {}
    void handleTcpAtCommand(const std::string& cmd) override {
        app.handleTcpAtCommand(cmd);
    }
    void setMonitorActive(bool active) override { app.setMonitorActive(active); }
    void resetDiscoveryBackoff() override { app.resetDiscoveryBackoff(); }
    int getWiFiState() const override { return app.getWiFiState(); }
};
} // namespace

static ArduinoTcpServer arduinoTcpServer(tcpServer, client);
static FirmwareAppTcpHostCallbacks tcpHostCallbacks(firmwareApp);
// authToken is the bare token; the vanilla prepends "AUTH " when comparing
// (TcpServerManager::isValidAuthToken builds "AUTH " + authToken).
static TcpServerManager tcpManager(arduinoTcpServer, statusLed,
                                   std::string(TCP_AUTH_TOKEN),
                                   tcpHostCallbacks);

// ── NTP Sync ─────────────────────────────────────────────────────────────────────
// NTP time is now owned entirely by FirmwareApp (owns NtpTimeSync + ArduinoSntp/
// ArduinoTimeNtp adapters). The inline initNtpSync()/ntpSyncCallback() were
// removed — NTP starts when WiFiManager reports connected (deferred out of the
// boot path). Discovery timestamps are now produced inside DiscoveryManager via
// its injected ITime adapter (ArduinoTime), so the .ino no longer carries a
// discovery timestamp fallback.

// ── UDP Discovery (delegated to FirmwareApp) ───────────────────────────────────
// Discovery broadcast (packet build, backoff cadence, signing, UDP send) now
// lives in the vanilla DiscoveryManager owned by FirmwareApp. The .ino no longer
// contains any discovery logic — it only (1) injects the build-time
// VEHICLE_SIM_ENABLE_DISCOVERY toggle and the live TCP-client state, and
// (2) resets the backoff timer on boot / buddy-disconnect, both via FirmwareApp.
// See FirmwareApp::update() which drives DiscoveryManager::update(now, haveClient)
// on every loop tick.

// ── AT Command Handling: delegate to vanilla AtCommandDispatcher ───────────
// The command structs, registry, and dispatch loop that used to live here were
// extracted into firmware/vanilla/AtCommandDispatcher (host + gmock tested). The
// .ino is now a thin veneer: it owns the five Arduino boundary adapters (above)
// and routes every command line to FirmwareApp, which owns the dispatcher.
//
// The dispatcher's sendTcpPrompt frames the reply as "<response>\r\r>" on the
// TCP client only (required by the host's TCPTransport HELO handshake); the
// serial path prints bare lines. The ATREBOOT flush-hang fix is preserved: the
// vanilla AtrebootCommandHandler returns shouldFlushClient=false, so the only
// flush is the prompt's, and ESP.restart() proceeds without hanging on a
// dead/half-closed socket.

static void drainSerialATCommands() {
    static String serialCmd;

    while (Serial.available()) {
        const auto c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (!serialCmd.isEmpty()) {
                firmwareApp.handleSerialAtCommand(serialCmd.c_str());
                serialQuietUntilMs = millis() + Constants::SERIAL_QUIET_DURATION_MS;
                serialCmd = "";
            }
        } else {
            serialCmd += c;
            if (serialCmd.length() > Constants::MAX_SERIAL_CMD_LENGTH) {
                serialCmd = "";
            }
        }
    }
}


// ── TCP Server Lifecycle Management ───────────────────────────────────────────
// FirmwareApp (via WiFiManager) owns the tcpServerNeedsRestart flag; the .ino
// only reads/clears it through the FirmwareApp seam and performs the actual
// WiFiServer end/begin + client cleanup (hardware-side effects stay in the .ino).
static void restartTcpServerIfNeeded() {
    if (firmwareApp.shouldRestartTcpServer()) {
        Serial.printf("%sRestarting TCP server on IP change%s\r\n", YELLOW, NC);
        tcpServer.end();
        tcpServer.begin();
        firmwareApp.clearTcpServerRestartFlag();

        // Disconnect any existing client since IP changed
        if (client.connected()) {
            client.stop();
            Serial.println("TCP: disconnected client due to IP change");
        }
    }
}

// ── FirmwareApp Callback Handlers ──────────────────────────────────────────────────
// Bridge FirmwareApp signals to .ino-side hardware effects (TCP/Discovery/OTA).
// The TCP-restart flag itself is owned by WiFiManager (set on the CONNECTED_STA /
// RECONNECTING transition); this callback is a post-transition firmware-effect
// hook. The actual WiFiServer end/begin runs in loop() via
// restartTcpServerIfNeeded(), which reads firmwareApp.shouldRestartTcpServer().
static void onRestartTcpServer() {
    // No-op: WiFiManager already set its own tcpServerNeedsRestart flag on the
    // transition. restartTcpServerIfNeeded() picks it up next loop tick.
}

static void onBroadcastDiscovery() {
    // FirmwareApp owns DiscoveryManager and performs the UDP send itself; this
    // callback is a post-broadcast firmware-effect hook (DiscoveryManager fires it
    // after each successful send). Leave as a documented no-op for now — any
    // .ino-only side effect (e.g. LED pulse) would be added here.
}

// FirmwareCallbacks structure for FirmwareApp — constructed locally in setup()
// and copied into FirmwareApp via setCallbacks() (which stores its own copy).
// Kept out of global scope (S5421).

// Factory reset: check if GPIO0 (BOOT button) is held at boot
static bool checkFactoryReset() {
    pinMode(Constants::FACTORY_RESET_PIN, INPUT_PULLUP);

    // Check if button is pressed (low = pressed on GPIO0 with pullup)
    if (digitalRead(Constants::FACTORY_RESET_PIN) == LOW) {
        Serial.printf("%sFactory reset: GPIO0 held at boot, waiting %lums to confirm...%s\r\n",
                        YELLOW, static_cast<unsigned long>(Constants::FACTORY_RESET_HOLD_MS), NC);

        // Wait to see if button continues to be held
        uint32_t heldMs = 0;
        while (heldMs < Constants::FACTORY_RESET_HOLD_MS) {
            if (digitalRead(Constants::FACTORY_RESET_PIN) != LOW) {
                Serial.printf("%sFactory reset: released early, cancelling%s\r\n", YELLOW, NC);
                return false;
            }
            delay(100);
            heldMs += 100;
        }

        // Button held for full duration - clear WiFi credentials
        Serial.printf("%sFactory reset: clearing WiFi credentials and booting to AP mode%s\r\n",
                        RED, NC);
        firmwareApp.clearCredentials();
        return true;
    }
    return false;
}

void setup() {
    Serial.begin(Constants::SERIAL_BAUD);

    // Initialize StatusLED first - turns LED OFF, then sets BOOT pattern
    statusLed.init();

    // Factory reset check before WiFi init - allows wiping stored credentials
    // Same firmware, no reflash needed. Hold BOOT button (GPIO0) during boot.
    (void)checkFactoryReset();

    // ── Initialize FirmwareApp (replaces inline WiFi state machine) ───────────────
    // FirmwareApp.init() sets up WiFiManager and drives initial connection
    firmwareApp.init();
    firmwareApp.setCallbacks(esp32_firmware::FirmwareCallbacks{
        .restartTcpServer = onRestartTcpServer,
        .broadcastDiscovery = onBroadcastDiscovery
    });

    // ── WiFi Event Handlers ───────────────────────────────────────────────────────────
    // Bridge Arduino WiFi STA-disconnect events straight to FirmwareApp, which owns
    // the WiFi state machine (WiFiManager::onDisconnected handles auth-failure → AP
    // fallback and non-auth → RECONNECTING). The 2-param lambda matches WiFiEventCb
    // (WiFi.onEvent overload for WiFiEvent_t); info.wifi_sta_disconnected.reason
    // carries the disconnect cause code.
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            firmwareApp.onWiFiDisconnected(info.wifi_sta_disconnected.reason);
        }
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    // TWAI init — listen-only so we never transmit on the vehicle bus
#if VEHICLE_SIM_ENABLE_TWAI
    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(Constants::TWAI_TX, Constants::TWAI_RX, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&gcfg, &tcfg, &fcfg) != ESP_OK) {
        Serial.printf("%sFAIL: twai_driver_install%s\r\n", RED, NC);
        while (true) delay(1000);
    }
    if (twai_start() != ESP_OK) {
        Serial.printf("%sFAIL: twai_start%s\r\n", RED, NC);
        while (true) delay(1000);
    }
    Serial.println("TWAI started @ 500kbps (listen-only)");
#else
    Serial.println("TWAI disabled via VEHICLE_SIM_ENABLE_TWAI=0");
#endif

    // NOTE: WiFi state machine is driven by FirmwareApp (WiFiManager). init() ran
    // the first state-machine tick; loop() drives subsequent ticks via update().

    // Initialize discovery: hand FirmwareApp the build-time enable flag, then reset
    // the vanilla DiscoveryManager's backoff timer (replaces the inline
    // resetDiscoveryBackoff()). The UDP socket itself is opened lazily by
    // FirmwareApp on the first loop tick (deferred out of the boot path).
    firmwareApp.setDiscoveryEnabled(VEHICLE_SIM_ENABLE_DISCOVERY);
    firmwareApp.resetDiscoveryBackoff();

    // NOTE: NTP sync is NO LONGER started here at boot. Starting SNTP/sockets
    // during setup() crashed the ESP32 (netif not up). FirmwareApp now starts
    // NtpTimeSync from the WiFi-connected event (deferred into loop()/update()).

    // Start TCP server (will be restarted on WiFi reconnect/IP change)
    tcpServer.begin();
    Serial.printf("TCP listening on port %u\r\n", Constants::TCP_PORT);

    // Initialize device ID from MAC address
    std::array<uint8_t, 6> mac;
    WiFi.macAddress(mac.data());
    discoveryDeviceId.fill(0);
    std::copy(mac.begin(), mac.end(), discoveryDeviceId.begin());

    // Wire the AT command boundary adapters into FirmwareApp. The deviceId is now
    // populated (above), so the dispatcher reads the live array when a command is
    // first handled. This hands the five Arduino adapters (TCP client / serial /
    // ESP restart / NVS WiFi store / monitor state) to the vanilla
    // AtCommandDispatcher that FirmwareApp owns.
    firmwareApp.setAtCommandAdapters(arduinoAtTcpClient, arduinoAtSerial, arduinoAtEsp,
                                     arduinoAtWifiStore, arduinoAtMonitor, discoveryDeviceId);

    // Tagged boot diagnostic (carries the device-id tag once it is known)
    printTagged(GREEN, "CAN bridge ready");

    // UDP discovery socket is opened lazily by FirmwareApp on the first loop tick
    // (deferred out of the boot path — see setDiscoveryEnabled above). No inline
    // udpDiscovery.begin() here; the .ino owns no discovery UDP state.

    // OTA: first mark THIS firmware's boot as healthy (cancels any pending
    // rollback from a previous OTA), then start the signed-image OTA server.
    // Order matters — mark-valid before bringing up the server so a rollback
    // condition is cleared before accepting new uploads.
#if VEHICLE_SIM_ENABLE_OTA_SERVER
    otaMarkValidOnBoot();
    otaSetup();
#else
    Serial.println("OTA server disabled via VEHICLE_SIM_ENABLE_OTA_SERVER=0");
#endif
}

void loop() {
    // 5-second heartbeat: prints current state model snapshot so the serial
    // monitor shows exactly what state the firmware thinks it's in.
    static uint32_t lastHeartbeatMs = 0;
    if (millis() - lastHeartbeatMs > 5000) {
        lastHeartbeatMs = millis();
        int wifiState = firmwareApp.getWiFiState();
        const char* stateName = "?";
        switch (static_cast<esp32_firmware::WiFiState::State>(wifiState)) {
            case esp32_firmware::WiFiState::State::DISCONNECTED:   stateName = "DISCONNECTED";   break;
            case esp32_firmware::WiFiState::State::CONNECTING:     stateName = "CONNECTING";     break;
            case esp32_firmware::WiFiState::State::CONNECTED_STA:  stateName = "CONNECTED_STA";  break;
            case esp32_firmware::WiFiState::State::CONNECTED_AP:   stateName = "CONNECTED_AP";   break;
            case esp32_firmware::WiFiState::State::RECONNECTING:   stateName = "RECONNECTING";   break;
        }
        Serial.printf("[STATE] uptime=%lums wifi=%s monitor=%s\r\n",
                      millis(),
                      stateName,
                      firmwareApp.isMonitorActive() ? "ACTIVE" : "idle");
    }

    // ── Update FirmwareApp (drives WiFiManager + StatusLED + Discovery) ────────────
    // Tell FirmwareApp the live TCP-client state so DiscoveryManager can suppress
    // broadcasts while a buddy is connected. FirmwareApp.update() calls
    // WiFiManager.update(), statusLed.update(), and DiscoveryManager.update().
    firmwareApp.setClientConnected(client && client.connected());
    firmwareApp.update(millis());

    // NOTE: statusLed.update() and discovery broadcast are now driven by
    // FirmwareApp.update(); no separate calls needed here.

    // Restart TCP server if WiFi reconnected with new IP
    restartTcpServerIfNeeded();

    // Service any incoming OTA upload before other work so an update isn't
    // starved by CAN traffic. Non-blocking: handles at most one connection.
#if VEHICLE_SIM_ENABLE_OTA_SERVER
    otaLoop();
#endif

    // Discovery broadcasting is driven entirely by FirmwareApp.update() above
    // (which calls DiscoveryManager::update on the cadence). No inline broadcast
    // logic remains in the .ino.

    // ── TCP accept/auth/dispatch (Stage 6: delegated to vanilla TcpServerManager) ────
    // One tick of the client-facing TCP lifecycle (accept → AUTH → command-dispatch →
    // disconnect cleanup + LED revert). The inline loop was deleted; the manager
    // drives it through ArduinoTcpServer/ArduinoTcpServerClient + the
    // FirmwareAppTcpHostCallbacks seam. The global `client` stays the single
    // connection truth source (ArduinoTcpServer::accept assigns into it), so
    // setClientConnected / ArduinoTcpClient above remain in sync.
    tcpManager.cycle(static_cast<uint32_t>(millis()));

    drainSerialATCommands();

    // Always drain the TWAI RX queue through the vanilla CanBridge. CanBridge
    // dispatches each frame to Serial unconditionally, and to TCP only when a
    // client is connected AND monitorActive. The serial quiet-window is passed
    // in so CanBridge suppresses serial emission during that window (keeps
    // serial logging live otherwise, with no WiFi client). Single RX drain —
    // never double-reads a frame.
#if VEHICLE_SIM_ENABLE_TWAI
    firmwareApp.processCanFrames(serialQuietUntilMs);
#endif
}

