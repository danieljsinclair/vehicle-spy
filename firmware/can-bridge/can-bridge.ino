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
// ── StatusLED Class Definitions ─────────────────────────────────────────────────────
// Declarative pattern-based LED implementation with SOLID principles and TDD
#include "StatusLED.h"
#include "HardwareStatusLEDOutput.h"
// ── FirmwareApp Components ───────────────────────────────────────────────────────────
// Vanilla firmware orchestrator (WiFi/LED state machine + callback seams)
#include "ArduinoWiFi.h"
#include "ArduinoDebugSerial.h"
#include "ArduinoPreferences.h"
#include "ArduinoUdp.h"
#include "ArduinoTime.h"
#include "ArduinoSntp.h"
#include "ArduinoTimeNtp.h"
#include "FirmwareApp.h"
#include "LoopHeartbeat.h"
#include "NvsWifiCredentialStore.h"  // vanilla storeWifiCredentials (NVS write logic)
#include "CanDriver.h"
// ── TcpServerManager (Stage 6 extraction) ────────────────────────────────────────────
// Vanilla accept/auth/dispatch state machine (host-tested, 14 tests). The .ino
// supplies the WiFiServer/WiFiClient adapters + a narrow ITcpHostCallbacks impl
// backed by firmwareApp. Inline TCP loop deleted (was loop() L634-696).
#include "ITcpServer.h"
#include "TcpServerManager.h"
#include "TcpManagerConnectionSource.h"
#include "ArduinoTcpServer.h"
// ── SerialCommandFramer (Phase 2 extraction) ─────────────────────────────────────────
// Vanilla serial line-framer (host-tested, 10 tests). Owns the CR/LF framing,
// empty-line, and overflow-reset rules; the .ino supplies an ISerialSource
// backed by Serial + a handler that forwards to FirmwareApp. Inline framing
// loop deleted from drainSerialATCommands().
#include "SerialCommandFramer.h"
#include "DeviceTag.h"
#include "FactoryReset.h"
#include "FactoryResetCheck.h"
#include "ArduinoResetAdapters.h"

// VEHICLE_SIM_ENABLE_TWAI must default to ON (production failsafe) and be defined
// BEFORE ArduinoCanAdapters.h consumes it below. Only host tests override to 0 (-D).
// (Include-order bug: previously #defined at line ~243, AFTER this include, so the
//  stub ArduinoCanDriver — receive() returns -1 — was compiled into production.)
#ifndef VEHICLE_SIM_ENABLE_TWAI
#define VEHICLE_SIM_ENABLE_TWAI 1
#endif
#include "ArduinoCanAdapters.h"
#include "ArduinoAtAdapters.h"
#include "ArduinoSerialSource.h"
struct SerialEventLogger;  // Forward decl for arduino auto-prototype of serialEventLogger()
// Forward declaration (cpp:S5421 composite): TimeAdapters is defined later in
// this TU and returned by reference from the timeAdapters() accessor; this
// satisfies Arduino's auto-generated function prototypes (hoisted above the
// struct definition). See the struct + accessor where the former globals were.
struct TimeAdapters;
struct CanAdapters;   // cpp:S5421 composite (C5): defined later, returned by canAdapters()
struct AtAdapters;    // cpp:S5421 composite (C6): defined later, returned by atAdapters()

// DEFERRED: this .ino accumulates WiFi/AT/discovery/OTA/StatusLED handlers in one translation unit (SRP). Extract to separate .cpp units when adding the next handler.

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
using esp32_firmware::TcpManagerConnectionSource;
using esp32_firmware::ArduinoTcpServer;
using esp32_firmware::ITcpHostCallbacks;
using esp32_firmware::SerialCommandFramer;
using esp32_firmware::LoopHeartbeat;
using esp32_firmware::ArduinoResetGpio;
using esp32_firmware::ArduinoResetDelay;
using esp32_firmware::ArduinoResetLogger;
using esp32_firmware::ArduinoCanDriver;
using esp32_firmware::ArduinoTcpClient;
using esp32_firmware::ArduinoSerialCan;
using esp32_firmware::ArduinoTwaiLogger;
using esp32_firmware::ArduinoTwaiHardware;
using esp32_firmware::ArduinoAtTcpClient;
using esp32_firmware::ArduinoAtSerial;
using esp32_firmware::ArduinoAtEsp;
using esp32_firmware::ArduinoAtWifiStore;
using esp32_firmware::ArduinoSerialSource;

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
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;

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
// cpp:S5421: was a mutable global. Function-local static accessor — the NVS
// Preferences handle for WiFi SSID/pass, opened/closed around each credential
// write in storeWifiCredentials() below.
Preferences& wifiCredentials() { static Preferences inst; return inst; }

// NVS keys for WiFi credentials storage are defined in
// firmware/vanilla/NvsWifiCredentialStore.cpp (the vanilla storeWifiCredentials
// function owns the namespace + key constants; this veneer delegates to it).

// cpp:S5421: was a mutable global array. Function-local static accessor; filled
// from the MAC in setup() (mutated at boot — fine for a function-local static).
// Read by reference by FirmwareApp (ctor + setAtCommandAdapters).
std::array<uint8_t, 16>& discoveryDeviceId() { static std::array<uint8_t, 16> inst; return inst; }

// ── Message Tagging ────────────────────────────────────────────────────────────
// Tag Serial diagnostic messages with device ID for clarity in monitor output.
// The format logic lives in vanilla (formatDeviceTag) so it is host-testable;
// this veneer converts the std::string result to Arduino String for Serial.printf.
static String deviceMessageTag() {
    return String(esp32_firmware::formatDeviceTag(discoveryDeviceId()).c_str());
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

// WiFi credentials removed from build flags. Creds live in NVS only; provision
// via ATSETWIFI or `make set-wifi-creds`. Firmware boots AP-first until creds
// are stored (FirmwareApp falls back to AP mode when no NVS creds exist).
static constexpr const char* WIFI_SSID = nullptr;
static constexpr const char* WIFI_PASSWORD = nullptr;

static constexpr const char* AP_SSID = "ESP32-CAN";
static constexpr const char* AP_PASS = "cancan12";

// ── Status LED ─────────────────────────────────────────────────────────────────
// Visual feedback using the blue LED on GPIO2
// cpp:S5421: were mutable globals. Function-local static accessors; statusLed's
// accessor calls ledOutput() so the init order is deterministic (ledOutput first).
HardwareStatusLEDOutput& ledOutput() { static HardwareStatusLEDOutput inst(2); return inst; }  // GPIO2 for ESP32 blue LED
StatusLED& statusLed() { static StatusLED inst(&ledOutput()); return inst; }

// ── FirmwareApp Components ─────────────────────────────────────────────────────
// Arduino adapters for vanilla interfaces (scoped to .ino via ARDUINO ifdef)
// cpp:S5421: were mutable globals. Function-local static accessors (WiFi /
// Preferences / UDP Arduino adapters injected into FirmwareApp).
ArduinoWiFi& arduinoWiFi() { static ArduinoWiFi inst; return inst; }
ArduinoPreferences& arduinoPrefs() { static ArduinoPreferences inst; return inst; }

// WiFi state-transition trace sink (ISerial), injected into WiFiManager via
// FirmwareApp. Function-local static accessor (cpp:S5421).
esp32_firmware::ArduinoDebugSerial& arduinoDebugSerial() {
    static esp32_firmware::ArduinoDebugSerial inst;
    return inst;
}
ArduinoUdp& arduinoUdp() { static ArduinoUdp inst; return inst; }
// cpp:S5421 (composite): were 3 mutable globals (arduinoTime/Sntp/TimeNtp).
// Grouped into a struct held by a function-local static accessor — the struct
// instance is function-local (not namespace-scope), so S5421 does not flag it,
// clearing all 3 issues. Members stay default-constructed (same init timing as
// the prior file-scope statics: first call is during firmwareApp construction).
struct TimeAdapters {
    ArduinoTime time;
    ArduinoSntp sntp;        // ISntp adapter for NTP sync
    ArduinoTimeNtp timeNtp;  // ITimeNtp adapter for NTP sync
};
TimeAdapters& timeAdapters() { static TimeAdapters inst; return inst; }

// Baked credentials removed: creds now live in NVS only.
// FirmwareApp falls back to AP mode when no NVS creds exist (user provisions
// via ATSETWIFI / make set-wifi-creds).
static constexpr const char* BAKED_SSID = nullptr;
static constexpr const char* BAKED_PASS = nullptr;

// ── CAN Bridge Arduino Adapters ──────────────────────────────────────────
// Thin adapters implementing CanBridge's vanilla interfaces over the ESP32
// hardware. Construction/install of the TWAI driver happens once in setup()
// (hardware-touching), so the adapter only READS frames here (safe post-boot).
// NOTE: the connected-buddy WiFiClient must be declared before these adapters
// because their member bodies reference it (complete-class context).
// cpp:S5421: was `static WiFiClient client;`. Function-local static accessor —
// the single connected-buddy WiFiClient (connection truth source, assigned into
// by ArduinoTcpServer::accept).
WiFiClient& client() { static WiFiClient inst; return inst; }
struct CanAdapters {
    ArduinoCanDriver canDriver;
    ArduinoTcpClient tcpClient{client()};
    ArduinoSerialCan serialCan;
    esp32_firmware::CanBridgeDeps deps{canDriver, tcpClient, serialCan};
};
CanAdapters& canAdapters() { static CanAdapters inst; return inst; }


// ── AT Command Adapters (vanilla AtCommandDispatcher boundaries) ────────────
// Thin Arduino implementations of the vanilla AT-boundary interfaces. The .ino
// owns the hardware objects (WiFiClient/Serial/ESP/Preferences); FirmwareApp owns
// the AtCommandDispatcher and is handed these adapters so the device-specific
// I/O stays in the .ino while the command logic lives in vanilla code.
// Forward-declare FirmwareApp so adapter methods can reference it (static init order).
class FirmwareApp;
extern FirmwareApp firmwareApp;

// cpp:S5421 (composite, C6): were 4 mutable globals (arduinoAtTcpClient/Serial/
// Esp/WifiStore). Grouped into an AtAdapters struct held by a function-local
// static accessor (struct instance is function-local -> not flagged; clears all
// 4). The monitor-state boundary is no longer adapted here — FirmwareApp
// implements IMonitorState directly (passed to its own AtCommandDispatcher).
// Token store + credential-clear adapters: thin wrappers that delegate to
// FirmwareApp (which owns the NVS write boundary). Defined here (not in
// ArduinoAtAdapters.h) because they reference FirmwareApp, which is declared
// later in this TU.
struct FirmwareTokenStore : public esp32_firmware::IWifiTokenStore {
    explicit FirmwareTokenStore(esp32_firmware::FirmwareApp& app) : app_(app) {}
    bool storeToken(const std::string& token) override { return app_.tokenStore().store(token); }
    esp32_firmware::FirmwareApp& app_;
};

struct FirmwareCredentialClearAt : public esp32_firmware::IWifiCredentialClear {
    explicit FirmwareCredentialClearAt(esp32_firmware::FirmwareApp& app) : app_(app) {}
    bool clear() override { app_.clear(); return true; }
    esp32_firmware::FirmwareApp& app_;
};

struct AtAdapters {
    ArduinoAtTcpClient tcpClient{client()};
    ArduinoAtSerial serial;
    ArduinoAtEsp esp{Constants::TCP_REBOOT_DELAY_MS};
    ArduinoAtWifiStore wifiStore{wifiCredentials()};
    FirmwareTokenStore tokenStore{firmwareApp};
    FirmwareCredentialClearAt credClear{firmwareApp};
};
AtAdapters& atAdapters() { static AtAdapters inst; return inst; }

// FirmwareApp orchestrator - delegates WiFi/LED/NTP/CAN to vanilla managers
// ArduinoWiFi implements both IWiFi and IWiFiDiscovery
// NTP is routed through FirmwareApp (owns NtpTimeSync + ArduinoSntp/ArduinoTimeNtp)
// CanBridge is constructed inside FirmwareApp from the adapter bundle above.
FirmwareApp firmwareApp(arduinoWiFi(), arduinoPrefs(), statusLed(), arduinoDebugSerial(),
                              arduinoWiFi(), arduinoUdp(), timeAdapters().time,
                              timeAdapters().sntp, timeAdapters().timeNtp,
                              discoveryDeviceId(),
                              canAdapters().deps,
                              nullptr,  // clientConnectionSource deferred to setup()
                              BAKED_SSID, BAKED_PASS);

// cpp:S5421: was `static WiFiServer tcpServer(Constants::TCP_PORT);`. Function-
// local static accessor — the TCP listener, end/begun on IP change in loop().
WiFiServer& tcpServer() { static WiFiServer inst(Constants::TCP_PORT); return inst; }

// ── TCP Server Manager wiring (Stage 6) ──────────────────────────────────────────────
// ArduinoTcpServer adapts the global tcpServer + client (the single connection
// truth source); TcpServerManager drives the accept/auth/dispatch lifecycle.
// FirmwareApp itself implements ITcpHostCallbacks, so the 4 out-of-SRP behaviours
// (command dispatch, monitor flag, discovery backoff, WiFi state) are reached
// directly — no forwarder adapter struct needed.
//
// tcpManager() and clientConnectionSource() are function-local statics to break
// the circular dependency: firmwareApp needs clientConnectionSource, which needs
// tcpManager, which needs firmwareApp. The references are stored but NOT
// dereferenced during construction (TcpServerManager only stores the ITcpHostCallbacks
// ref; TcpManagerConnectionSource only stores the TcpServerManager ref). By the
// time isClientConnected() is called in loop(), firmwareApp is fully constructed.
ArduinoTcpServer& arduinoTcpServer() {
    static ArduinoTcpServer inst(tcpServer(), client());
    return inst;
}
// Auth token loaded from NVS at boot (with baked default fallback).
// Initialized in setup() before the TCP server starts; tcpManager() reads it
// at first construction so the AUTH gate uses the NVS value when present.
std::string& loadedAuthToken() { static std::string inst; return inst; }

TcpServerManager& tcpManager() {
    // Token provider bound to loadedAuthToken(): reads the live static on every
    // AUTH check, so the value populated by setup() from NVS is honoured even
    // though tcpManager() is statically constructed before setup() runs.
    static TcpServerManager inst(arduinoTcpServer(),
                                 []() -> const std::string& { return loadedAuthToken(); },
                                 firmwareApp);
    return inst;
}
// clientConnectionSource() wraps tcpManager() so FirmwareApp can query the
// manager's own view of client adoption (eliminating the global-WiFiClient
// desync that fed selectLedPattern with stale connection state).
TcpManagerConnectionSource& clientConnectionSource() {
    static TcpManagerConnectionSource inst(tcpManager());
    return inst;
}

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

// ── Serial Command Framer (Phase 2: delegate to vanilla SerialCommandFramer) ─────
// The serial line-framing rules (CR/LF termination, empty-line skip, overflow
// reset) now live in the vanilla SerialCommandFramer. The .ino supplies only the
// Serial-backed byte source (ArduinoSerialSource) and a handler that forwards
// each completed line to FirmwareApp + sets the serial-quiet window (the same two
// side effects the inline loop performed). cpp:S5421: the source + framer are
// held by a function-local static accessor pair (not namespace globals).
ArduinoSerialSource& serialSource() { static ArduinoSerialSource inst; return inst; }
SerialCommandFramer& serialFramer() {
    static SerialCommandFramer inst(serialSource(), Constants::MAX_SERIAL_CMD_LENGTH);
    return inst;
}

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
    // Delegate framing to the vanilla SerialCommandFramer (Phase 2 extraction).
    // The framer reads every available byte via ArduinoSerialSource and invokes
    // this handler once per complete, non-empty line — the same behavior the
    // inline loop performed (dispatch + serial-quiet window).
    serialFramer().drain([](const std::string& line) {
        firmwareApp.atDispatcher().handleSerialCommand(line.c_str());
        firmwareApp.canBridge().setSerialQuietUntilMs(millis() + Constants::SERIAL_QUIET_DURATION_MS);
    });
}


// ── TCP Server Lifecycle Management ───────────────────────────────────────────
// FirmwareApp (via WiFiManager) owns the tcpServerNeedsRestart flag; the .ino
// only reads/clears it through the FirmwareApp seam and performs the actual
// WiFiServer end/begin + client cleanup (hardware-side effects stay in the .ino).
static void restartTcpServerIfNeeded() {
    if (firmwareApp.tcpRestartPolicy().shouldRestart()) {
        // The TCP server restart is real (end/begin), but the user-facing
        // serial message is intentionally omitted: the [STATE]/[EVENT] stream
        // (emitted by FirmwareApp on WIFI_CONNECTED) already conveys the
        // reconnect. The old "Restarting TCP server on IP change" message
        // was misleading — the IP usually did NOT change (same DHCP lease
        // after a brief blip). The restart reason (old vs new IP) is an
        // internal-model detail, not a user-facing diagnostic.
        tcpServer().end();
        tcpServer().begin();
        firmwareApp.tcpRestartPolicy().clear();

        // Disconnect any existing client ONLY when the listening socket was
        // genuinely rebound (a reconnect/IP-change). The manager owns the single
        // authoritative client slot (TcpServerManager::hasClient()), so we must
        // not stomp a live, mid-handshake client that belongs to the manager.
        // The unconditional client().stop() here was the regression: moving the
        // restart decision to "always restart on (re)connect" (resilient-reconnect)
        // turned the old reconnect-only stomp into a perpetual client kill —
        // the socket rebinds every transition and client().stop() severs the
        // connection before the AUTH handshake can complete, so the host sees
        // client_connected then an immediate reason=0 disconnect with no reply.
        if (clientConnectionSource().isClientConnected() && client().connected()) {
            client().stop();
        }
    }
}

// FirmwareApp owns DiscoveryManager and performs the UDP send itself; discovery
// broadcast callbacks are no-ops in the .ino.

// ── Serial Event Logger (centralized observability) ──────────────────────────
// IEventLogger implementation that writes [EVENT] and [STATE] lines to Serial.
// FirmwareApp owns a pointer to this instance and emits ALL observability lines
// through it (no per-manager logger injection).
struct SerialEventLogger : public esp32_firmware::IEventLogger {
    void logEvent(const char* eventType, const std::string& detail) override {
        Serial.printf("[EVENT] %-20s %s\r\n", eventType, detail.c_str());
    }
    void logState(const std::string& line) override {
        Serial.print(line.c_str());
    }
};
SerialEventLogger& serialEventLogger() {
    static SerialEventLogger inst;
    return inst;
}

// Factory reset: check if GPIO0 (BOOT button) is held at boot.
// The debounce/threshold logic lives in vanilla (FactoryResetCheck) so it
// is host-testable; this veneer owns the GPIO read + delay (hardware/timing).
//
// Arduino adapters for FactoryResetCheck boundaries.
// No ArduinoCredClear adapter: FirmwareApp implements ICredentialClear directly.

static bool checkFactoryReset() {
    pinMode(Constants::FACTORY_RESET_PIN, INPUT_PULLUP);

    ArduinoResetGpio gpio(Constants::FACTORY_RESET_PIN);
    ArduinoResetDelay delayAdapter;
    ArduinoResetLogger logger(RED, YELLOW, NC);

    // firmwareApp is the ICredentialClear (wipes stored WiFi credentials on a
    // confirmed hold) — no forwarder adapter struct needed.
    esp32_firmware::FactoryResetCheck checker(
        Constants::FACTORY_RESET_HOLD_MS, 100, gpio, delayAdapter, logger, firmwareApp);
    return checker.run();
}

void setup() {
    Serial.begin(Constants::SERIAL_BAUD);

    // Initialize StatusLED first - turns LED OFF, then sets BOOT pattern
    statusLed().init();

    // Factory reset check before WiFi init - allows wiping stored credentials
    // Same firmware, no reflash needed. Hold BOOT button (GPIO0) during boot.
    (void)checkFactoryReset();

    // Load auth token from NVS (with baked default fallback) before TCP server starts.
    // ATSETTOKEN writes the token to NVS; on boot we read it here and fall back to
    // the build-time default if NVS has none.
    loadedAuthToken();  // initialize the static
    std::string nvsToken = firmwareApp.tokenStore().loadOrDefault();
    loadedAuthToken() = nvsToken.empty() ? std::string(TCP_AUTH_TOKEN) : nvsToken;

    // Wire tcpManager + clientConnectionSource now that firmwareApp is fully
    // constructed and NVS token is loaded (avoids static-init-order capture
    // of an incompletely-constructed FirmwareApp).
    firmwareApp.setClientConnectionSource(&clientConnectionSource());

    // ── Initialize FirmwareApp (replaces inline WiFi state machine) ───────────────
    // FirmwareApp.init() sets up WiFiManager and drives initial connection
    firmwareApp.init();

    // Latency fix (Phase 1): disable WiFi modem-sleep on the CAN path. With
    // sleep enabled the ESP32 radios can gate the link between beacons, adding
    // 10-100ms of scheduling latency to every burst — exactly the variable
    // stalls we are trying to remove. WIFI_PS_NONE keeps the radio awake.
    WiFi.setSleep(false);

    firmwareApp.setEventLogger(serialEventLogger());

    // ── WiFi Event Handlers ───────────────────────────────────────────────────────────
    // Bridge Arduino WiFi STA-disconnect events straight to FirmwareApp, which owns
    // the WiFi state machine (WiFiManager::onDisconnected handles auth-failure → AP
    // fallback and non-auth → WIFI_CONNECTING). The 2-param lambda matches WiFiEventCb
    // (WiFi.onEvent overload for WiFiEvent_t); info.wifi_sta_disconnected.reason
    // carries the disconnect cause code.
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            firmwareApp.onWiFiDisconnected(info.wifi_sta_disconnected.reason);
        }
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    // TWAI init — listen-only so we never transmit on the vehicle bus
    // Delegate to vanilla CanDriver (enabled/disabled branch, install/start
    // sequence, error logging). The adapters below bridge ESP32 hardware.
    ArduinoTwaiLogger twaiLogger(RED, NC);
    ArduinoTwaiHardware twaiHardware;
    esp32_firmware::CanDriver twaiInit(twaiLogger, twaiHardware, VEHICLE_SIM_ENABLE_TWAI);

#if VEHICLE_SIM_ENABLE_TWAI
    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(Constants::TWAI_TX, Constants::TWAI_RX, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (!twaiInit.initialize(
            static_cast<esp32_firmware::CanGeneralConfig*>(static_cast<void*>(&gcfg)),
            static_cast<esp32_firmware::CanTimingConfig*>(static_cast<void*>(&tcfg)),
            static_cast<esp32_firmware::CanFilterConfig*>(static_cast<void*>(&fcfg)))) {
        while (true) delay(1000);  // Hang on failure (preserves original fatal behavior)
    }
#else
    // Still call initialize() so the disabled-path log is emitted.
    (void)twaiInit.initialize(nullptr, nullptr, nullptr);
#endif

    // [CAN] receive-adapter mode at boot — the definitive TWAI indicator.
    // "TWAI started" above is NOT sufficient: the HW-init path runs even when the
    // receive adapter is the no-op stub (the include-order bug). This line tells
    // you production has the REAL receiver.
    Serial.printf("[CAN] receive adapter: %s (VEHICLE_SIM_ENABLE_TWAI=%d)\r\n",
                  VEHICLE_SIM_ENABLE_TWAI ? "REAL twai_receive" : "STUB returns-1 NO-FRAMES",
                  VEHICLE_SIM_ENABLE_TWAI);

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
    tcpServer().begin();
    Serial.printf("TCP listening on port %u\r\n", Constants::TCP_PORT);

    // Initialize device ID from MAC address
    std::array<uint8_t, 6> mac;
    WiFi.macAddress(mac.data());
    discoveryDeviceId().fill(0);
    std::copy(mac.begin(), mac.end(), discoveryDeviceId().begin());

    // Wire the AT command boundary adapters into FirmwareApp. The deviceId is now
    // populated (above), so the dispatcher reads the live array when a command is
    // first handled. This hands the four Arduino adapters (TCP client / serial /
    // ESP restart / NVS WiFi store) to the vanilla AtCommandDispatcher that
    // FirmwareApp owns. The monitor-state boundary is satisfied by firmwareApp
    // itself (it implements IMonitorState), so no adapter is passed for it.
    firmwareApp.setAtCommandAdapters(atAdapters().tcpClient, atAdapters().serial, atAdapters().esp,
                                     atAdapters().wifiStore, atAdapters().tokenStore, atAdapters().credClear,
                                     firmwareApp, discoveryDeviceId());

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
    // Drain serial AT commands FIRST — before any operation that can block or
    // delay the loop.  The ESP32 Arduino-core UART RX ring buffer is 256 bytes;
    // if loop() is delayed more than ~22 ms (the time for 256 bytes at 115200
    // baud) the buffer overflows and the UART RX locks up permanently
    // (arduino-esp32#6326, fixed in core 2.0.3).  Once locked, Serial.read()
    // returns -1 forever and the AT command path is inert until reboot.
    // Reading the serial input at the top of every tick minimises that window.
    drainSerialATCommands();

    // 5-second heartbeat: delegate to vanilla LoopHeartbeat which formats the
    // enriched state snapshot (client IP, discovery cadence, LED pattern).
    static LoopHeartbeat heartbeat(Constants::HEARTBEAT_INTERVAL_MS);
    if (heartbeat.tick(millis(),
                       firmwareApp.getWiFiState(),
                       firmwareApp.isMonitorActive(),
                       firmwareApp.getClientIp(),
                       firmwareApp.discoveryPolicy().cadenceString(millis()),
                       firmwareApp.ledPatternPolicy().currentPattern(),
                       firmwareApp.wifiManager().resolveTargetSsid(),
                       (firmwareApp.wifi().getMode() == 2) ? firmwareApp.wifi().softAPIP() : firmwareApp.wifi().localIP(),
                       firmwareApp.wifiManager().getAuthCampaignDetail())) {
        Serial.print(heartbeat.snapshot().c_str());
    }

    // ── TCP accept/auth/dispatch (Stage 6) BEFORE the FirmwareApp LED update ──────
    // FIX (fix/led-status DEFECT 1): cycle() MUST run BEFORE FirmwareApp.update()
    // so the client adopted on THIS tick (TcpServerManager::hasClient() becomes
    // true) is visible to selectLedPattern() inside update(). With the old order
    // — update() then cycle() — loop() queried clientConnectionSource() before
    // the manager had adopted the client, so a just-connected buddy fell through
    // to the wifi branch (WIFI_SEARCHING = mostly dark) instead of SOLID BLUE,
    // and restartTcpServerIfNeeded() (between them) could client().stop() the
    // freshly-adopted socket. cycle() runs first so update() sees the adoption.
    // restartTcpServerIfNeeded() still guards on isClientConnected() &&
    // client().connected() before stopping anything, so it cannot sever a socket
    // the manager still holds.
    tcpManager().cycle(static_cast<uint32_t>(millis()));

    // ── Update FirmwareApp (drives WiFiManager + StatusLED + Discovery) ────────────
    // FirmwareApp.update() calls WiFiManager.update(), statusLed.update(), and
    // DiscoveryManager.update(). The live TCP-client state is queried via
    // clientConnectionSource() (backed by TcpServerManager::hasClient()) inside
    // update() — no longer fed from the global WiFiClient's connected() which
    // desyncs from the manager's adopted-client view. Runs AFTER cycle() so a
    // client adopted this tick is observed immediately (SOLID BLUE).
    firmwareApp.update(millis());

    // NOTE: statusLed.update() and discovery broadcast are now driven by
    // FirmwareApp.update(); no separate calls needed here.

    // Restart TCP server if WiFi reconnected with new IP. Guarded on
    // isClientConnected() && client().connected(), so it cannot drop a socket
    // the manager still holds.
    restartTcpServerIfNeeded();

    // Service any incoming OTA upload before other work so an update isn't
    // starved by CAN traffic. Non-blocking: handles at most one connection.
#if VEHICLE_SIM_ENABLE_OTA_SERVER
    otaLoop();
#endif

    // Discovery broadcasting is driven entirely by FirmwareApp.update() above
    // (which calls DiscoveryManager::update on the cadence). No inline broadcast
    // logic remains in the .ino.

    // Always drain the TWAI RX queue through the vanilla CanBridge. CanBridge
    // dispatches each frame to Serial unconditionally, and to TCP only when a
    // client is connected AND monitorActive. The serial quiet-window is passed
    // in so CanBridge suppresses serial emission during that window (keeps
    // serial logging live otherwise, with no WiFi client). Single RX drain —
    // never double-reads a frame.
#if VEHICLE_SIM_ENABLE_TWAI
    firmwareApp.canBridge().processFrames(firmwareApp.isMonitorActive(), static_cast<uint32_t>(millis()));
#endif
}

