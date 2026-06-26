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

// ── Named Constants (no magic numbers) ──────────────────────────────────────
namespace Constants {
    // Network ports
    static constexpr uint32_t TCP_PORT = 3333;
    static constexpr uint16_t DISCOVERY_PORT = 3335;
    static constexpr uint16_t OTA_HTTP_PORT = 80;

    // Timing intervals (milliseconds)
    static constexpr uint32_t WIFI_CONNECT_RETRY_INTERVAL_MS = 5000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t WIFI_INITIAL_CONNECT_MAX_RETRIES = 120;
    static constexpr uint32_t SERIAL_BAUD = 115200;
    static constexpr uint32_t SERIAL_QUIET_DURATION_MS = 250;

    // TCP timeouts
    static constexpr uint32_t TCP_AUTH_TIMEOUT_MS = 5000;
    static constexpr uint32_t TCP_COMMAND_TIMEOUT_MS = 100;
    static constexpr uint32_t TCP_FLUSH_DELAY_MS = 250;
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
    // MARKER: FACTORY_RESET_HOOK - not TODO: (Sonar), future implementation
    static constexpr gpio_num_t FACTORY_RESET_PIN = GPIO_NUM_0;  // Boot button (GPIO0)
    static constexpr uint32_t FACTORY_RESET_HOLD_MS = 3000;      // Hold 3 seconds to trigger
}

// ── WiFi State Machine ──────────────────────────────────────────────────────
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
        wifi_err_reason_t lastDisconnectReason = WIFI_REASON_UNSPECIFIED;
        bool tcpServerNeedsRestart = false;
    };
}

// ── Forward Type Declarations (must be before any function use) ────────────────
// These type definitions must appear after WiFiState to avoid Arduino preprocessing issues

// Credential source enumeration for WiFi connection strategy
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
    const char* message;  // Optional diagnostic message

    StateTransition() : nextState(WiFiState::State::DISCONNECTED),
                       setTcpServerRestartFlag(false), initNtp(false), message(nullptr) {}

    StateTransition(WiFiState::State state, bool tcpRestart = false, bool ntp = false, const char* msg = nullptr)
        : nextState(state), setTcpServerRestartFlag(tcpRestart), initNtp(ntp), message(msg) {}
};

// WiFi state handler interface (State Pattern)
struct WiFiStateHandler {
    virtual StateTransition execute(uint32_t now, WiFiState::Context& ctx) = 0;
    virtual ~WiFiStateHandler() = default;
};

// AT command execution result
struct AtCommandResult {
    String response;
    bool shouldReboot;
    bool shouldFlushClient;

    AtCommandResult(const char* resp = "", bool reboot = false, bool flush = false)
        : response(resp), shouldReboot(reboot), shouldFlushClient(flush) {}
};

// AT command handler interface (Command Pattern)
struct AtCommandHandler {
    virtual bool matches(const String& normalizedCmd) const = 0;
    virtual AtCommandResult execute(const String& originalCmd) const = 0;
    virtual ~AtCommandHandler() = default;
};

// WiFi SET command parameters
struct SetWifiParams {
    String ssid;
    String password;
    bool valid;

    SetWifiParams() : valid(false) {}
};

// ── NTP State ───────────────────────────────────────────────────────────────────
namespace NtpState {
    struct Context {
        bool synced = false;
        uint32_t lastSyncMs = 0;
        uint32_t syncAttempts = 0;
    };
}

// ── Discovery State ────────────────────────────────────────────────────────────
namespace DiscoveryState {
    struct Context {
        uint32_t connectTimeMs = 0;      // When we started looking for a buddy
        uint32_t lastBroadcastMs = 0;
        bool backoffActive = false;
    };
}

static NtpState::Context ntpCtx;
static DiscoveryState::Context discoveryCtx;

// ANSI color codes for serial output
static const char* const RED    = "\033[0;31m";
static const char* const GREEN  = "\033[0;32m";
static const char* const PURPLE = "\033[0;35m";
static const char* const YELLOW  = "\033[0;33m";
static const char* const NC     = "\033[0m";

// ── NVS Storage (WiFi Credentials) ────────────────────────────────────────────
static Preferences wifiCredentials;  // NVS storage for WiFi SSID/password

// NVS keys for WiFi credentials storage
static constexpr const char* NVS_WIFI_NAMESPACE = "wifi";
static constexpr const char* NVS_WIFI_SSID = "ssid";
static constexpr const char* NVS_WIFI_PASS = "pass";

// Testable pure function: Check if WiFi credentials are stored in NVS
static bool hasStoredWifiCredentials() {
    wifiCredentials.begin(NVS_WIFI_NAMESPACE, true);  // Read-only
    size_t ssidLen = wifiCredentials.getBytesLength(NVS_WIFI_SSID);
    size_t passLen = wifiCredentials.getBytesLength(NVS_WIFI_PASS);
    wifiCredentials.end();
    return (ssidLen > 0 && passLen > 0);
}

// Load WiFi credentials from NVS
static bool loadWifiCredentials(String& ssid, String& pass) {
    wifiCredentials.begin(NVS_WIFI_NAMESPACE, true);  // Read-only
    size_t ssidLen = wifiCredentials.getBytesLength(NVS_WIFI_SSID);
    size_t passLen = wifiCredentials.getBytesLength(NVS_WIFI_PASS);

    if (ssidLen > 0 && passLen > 0) {
        char ssidBuf[33];  // Max SSID length
        char passBuf[65];  // Max password length
        ssidLen = wifiCredentials.getString(NVS_WIFI_SSID, ssidBuf, sizeof(ssidBuf));
        passLen = wifiCredentials.getString(NVS_WIFI_PASS, passBuf, sizeof(passBuf));
        wifiCredentials.end();
        ssid = String(ssidBuf);
        pass = String(passBuf);
        return true;
    }
    wifiCredentials.end();
    return false;
}

// Store WiFi credentials to NVS
static bool storeWifiCredentials(const String& ssid, const String& pass) {
    wifiCredentials.begin(NVS_WIFI_NAMESPACE, false);  // Read-write
    bool success = wifiCredentials.putString(NVS_WIFI_SSID, ssid) > 0;
    success = success && wifiCredentials.putString(NVS_WIFI_PASS, pass) > 0;
    wifiCredentials.end();
    return success;
}

// Clear WiFi credentials from NVS (factory reset)
static bool clearWifiCredentials() {
    wifiCredentials.begin(NVS_WIFI_NAMESPACE, false);  // Read-write
    wifiCredentials.clear();
    wifiCredentials.end();
    Serial.printf("%sWiFi credentials cleared from NVS%s\r\n", YELLOW, NC);
    return true;
}

// Device ID declaration - needed for message tagging
static uint8_t discoveryDeviceId[16];

// ── Message Tagging ────────────────────────────────────────────────────────────
// Tag Serial diagnostic messages with device ID for clarity in monitor output
static String deviceMessageTag() {
    char tag[32];
    snprintf(tag, sizeof(tag), "[%02X%02X%02X%02X] ",
             discoveryDeviceId[0], discoveryDeviceId[1],
             discoveryDeviceId[2], discoveryDeviceId[3]);
    return String(tag);
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

static WiFiServer tcpServer(Constants::TCP_PORT);
static WiFiClient client;
static bool monitorActive = false;
static uint32_t serialQuietUntilMs = 0;
static WiFiState::Context wifiCtx;

// ── UDP Discovery Broadcast ──────────────────────────────────────────────
// Broadcasts unsigned discovery packets on UDP port 3335 so that CLI and iOS
// apps can auto-discover this ESP32 without manual IP configuration.
static uint32_t lastDiscoveryBroadcast = 0;
static WiFiUDP udpDiscovery;

// ── NTP Sync ─────────────────────────────────────────────────────────────────────
// NTP sync callback - called when time sync completes
static void ntpSyncCallback(struct timeval* tv) {
    // Only log on first successful sync, not on periodic re-syncs
    if (!ntpCtx.synced) {
        ntpCtx.synced = true;
        ntpCtx.syncAttempts = 0;
        // Convert Unix timestamp to human-readable UTC time
        time_t utcTime = tv->tv_sec;
        struct tm* utcInfo = gmtime(&utcTime);
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S UTC", utcInfo);
        Serial.printf("%sNTP synced: %s%s\r\n", GREEN, timeBuf, NC);
    }
    // Always update last sync time for monitoring
    ntpCtx.lastSyncMs = millis();
}

// Initialize NTP sync - call this when WiFi connects
static void initNtpSync() {
    if (ntpCtx.synced) return;
    // SNTP_OPMODE_POLL retries internally on NTP_RETRY_INTERVAL_MS — never re-init
    // while it's already running: sntp_setoperatingmode() asserts ("Operating mode
    // must not be set while SNTP client is running") → boot loop. Idempotent across
    // reconnects/retries. If NTP never syncs, getCurrentTimestamp() falls back to uptime.
    if (sntp_enabled()) return;

    ntpCtx.syncAttempts++;
    if (ntpCtx.syncAttempts > Constants::NTP_SYNC_RETRY_MAX) {
        Serial.printf("%sNTP sync: max attempts reached, using fallback time%s\r\n", YELLOW, NC);
        return;
    }

    Serial.printf("NTP: syncing time (attempt %u)...", ntpCtx.syncAttempts);

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_sync_interval(Constants::NTP_RETRY_INTERVAL_MS);
    sntp_set_time_sync_notification_cb(ntpSyncCallback);
    sntp_init();

    // Set timezone to UTC
    setenv("TZ", "UTC", 1);
    tzset();
}

// Get current timestamp with NTP sync fallback
static uint64_t getCurrentTimestamp() {
    time_t now = time(nullptr);
    if (now < 1000000) {  // If time is clearly invalid (before Sept 2001)
        // Fallback to uptime-based timestamp (milliseconds since boot / 1000)
        // This won't be real Unix time but will be monotonically increasing
        return millis() / 1000;
    }
    return now;
}

// ── Discovery Backoff ───────────────────────────────────────────────────────────
// Testable pure function: Calculate discovery interval based on age (testable unit)
static uint32_t discoveryIntervalMs(uint32_t ageMs) {
    if (ageMs < Constants::DISCOVERY_AGE_1_MIN_MS) {
        return Constants::DISCOVERY_INTERVAL_FAST_MS;
    } else if (ageMs < Constants::DISCOVERY_AGE_5_MIN_MS) {
        return Constants::DISCOVERY_INTERVAL_1_5_MIN_MS;
    } else if (ageMs < Constants::DISCOVERY_AGE_10_MIN_MS) {
        return Constants::DISCOVERY_INTERVAL_5_10_MIN_MS;
    } else if (ageMs < Constants::DISCOVERY_AGE_15_MIN_MS) {
        return Constants::DISCOVERY_INTERVAL_10_15_MIN_MS;
    } else if (ageMs < Constants::DISCOVERY_AGE_30_MIN_MS) {
        return Constants::DISCOVERY_INTERVAL_15_30_MIN_MS;
    } else {
        return Constants::DISCOVERY_INTERVAL_SLOW_MS;
    }
}

// Reset discovery backoff timer - call on boot and disconnect
static void resetDiscoveryBackoff() {
    discoveryCtx.connectTimeMs = millis();
    discoveryCtx.backoffActive = true;
    Serial.printf("%sDiscovery backoff timer reset%s\r\n", YELLOW, NC);
}

// ── Discovery ─────────────────────────────────────────────────────────────────
// Device ID (first 16 bytes of MAC address, hex) - declared earlier for message tagging
// Testable pure function: Build discovery packet header (testable unit)
static void buildDiscoveryPacket(uint8_t* packet, const uint8_t* deviceId, uint32_t tcpPort, uint16_t otaPort) {
    // Magic: "VSIM"
    packet[0] = 0x56; packet[1] = 0x53; packet[2] = 0x49; packet[3] = 0x4D;
    // Version
    packet[4] = 1;
    // Packet type: 1 = discovery
    packet[5] = 1;
    // Device ID (16 bytes)
    memcpy(packet + 6, deviceId, 16);
    // Nonce (8 bytes) — use millis as simple nonce
    uint32_t nonceVal = millis();
    memcpy(packet + 22, &nonceVal, 4);
    memset(packet + 26, 0, 4);
    // Timestamp (8 bytes, big-endian Unix epoch) - use NTP-synced time with fallback
    uint64_t now = getCurrentTimestamp();
    for (int i = 7; i >= 0; --i) {
        packet[30 + i] = (uint8_t)(now & 0xFF);
        now >>= 8;
    }
    // CAN port (2 bytes, big-endian)
    packet[38] = (tcpPort >> 8) & 0xFF;
    packet[39] = tcpPort & 0xFF;
    // OTA port (2 bytes, big-endian)
    packet[40] = (otaPort >> 8) & 0xFF;
    packet[41] = otaPort & 0xFF;

    // Signature bytes remain zeroed for unsigned discovery broadcasts.
    // If signing is enabled, signDiscoveryPacket() will fill this in.
    memset(packet + 42, 0, 64);
}

// ── Discovery Signing (Optional/Guarded) ───────────────────────────────────────
// Sign discovery packet with Ed25519 private key for host verification.
// Format: Ed25519 signature (64 bytes) over packet[0..41] (first 42 bytes).
// The signature fills the reserved 64-byte field at packet[42..105].
//
// PROPOSED FORMAT for host-side verification:
//   - Signed data: packet[0..41] (42 bytes: magic, version, type, deviceId, nonce, timestamp, ports)
//   - Signature: Ed25519 (64 bytes) at packet[42..105]
//   - Key format: Ed25519 private seed (32 bytes), baked at build time
//   - Public key: Host needs the corresponding public key for verification
//
// COORDINATION with host-engineer:
//   The host should:
//   1. Extract the first 42 bytes (signed payload)
//   2. Extract the last 64 bytes (signature)
//   3. Verify using the device's Ed25519 public key (matching the baked private key)
//   4. Reject discovery packets with invalid signatures (or allow unsigned if key unknown)
//
// IMPLEMENTATION:
//   If VEHICLE_SIM_ENABLE_DISCOVERY_SIGNING=1 and libsodium is available:
//   - Sign the packet payload with the baked private key
//   - Fall back to unsigned (zeros) if signing fails or key not available
//   If disabled (default), packets remain unsigned (all zeros)
#if VEHICLE_SIM_ENABLE_DISCOVERY_SIGNING
#include <sodium.h>
#include "DiscoveryPrivateKey.h"  // Baked private key header (not committed)

static void signDiscoveryPacket(uint8_t* packet) {
    // Check if crypto is initialized and key is available
    static bool cryptoInitialized = false;
    static bool hasSigningKey = false;

    if (!cryptoInitialized) {
        if (sodium_init() < 0) {
            Serial.printf("%sDiscovery signing: libsodium init failed, using unsigned%s\r\n", YELLOW, NC);
            cryptoInitialized = true;
            return;
        }
        cryptoInitialized = true;

        // Check if signing key is baked (non-zero)
        // DiscoveryPrivateKey.h should define: static const uint8_t DISCOVERY_PRIVATE_KEY[32]
        #ifdef DISCOVERY_PRIVATE_KEY
        // Check if key is non-zero (has been baked)
        for (size_t i = 0; i < 32; i++) {
            if (DISCOVERY_PRIVATE_KEY[i] != 0) {
                hasSigningKey = true;
                break;
            }
        }
        #endif
    }

    if (!hasSigningKey) {
        // No signing key available - keep signature zeroed (unsigned)
        return;
    }

    // Sign the packet payload (first 42 bytes)
    // Signature format: Ed25519 over packet[0..41], result at packet[42..105]
    unsigned long long sigLen;
    #ifdef DISCOVERY_PRIVATE_KEY
    int signResult = crypto_sign_detached(
        packet + 42,              // Signature output (64 bytes)
        &sigLen,                  // Signature length
        packet,                   // Message to sign (42 bytes)
        42,                       // Message length
        DISCOVERY_PRIVATE_KEY    // Private key (32 bytes)
    );

    if (signResult != 0 || sigLen != 64) {
        // Signing failed - keep signature zeroed (unsigned)
        Serial.printf("%sDiscovery signing failed (code=%d), using unsigned%s\r\n",
                        YELLOW, signResult, NC);
        memset(packet + 42, 0, 64);
    }
    #else
    // No key defined - keep unsigned
    #endif
}
#else
// Stub when signing is disabled - packets remain unsigned
static void signDiscoveryPacket(uint8_t* packet) {
    // Signature field already zeroed by buildDiscoveryPacket()
    (void)packet;  // Suppress unused warning
}
#endif

// Broadcast a discovery packet. The packet reserves the same signature field as
// the CLI/iOS wire format; current firmware broadcasts unsigned discovery so
// first connection remains usable before OTA keys are flashed.
// IMPORTANT: Continues broadcasting during reconnects so host can always find device.
static void broadcastDiscovery() {
    // Always broadcast when no buddy connected - "welcome a buddy"
    // Also broadcast during reconnects so host can always find device
    const bool haveClient = client && client.connected();
    if (haveClient) return;  // Don't broadcast if we have a buddy

    // Broadcast in AP mode (always ready) or STA mode (if ready)
    // STA mode is ready when: connected, disconnected (was connected), or connecting
    // This allows broadcasting during initial connection and reconnects, not just when fully connected
    if (WiFi.getMode() == WIFI_AP) {
        // AP mode - always ready to broadcast
    } else if (WiFi.getMode() == WIFI_STA) {
        // STA mode - broadcast if WiFi is initialized (mode is set)
        // Don't check status here - allow broadcasting during connection/reconnection
        // UDP will fail silently if WiFi isn't truly ready, which is acceptable
    } else {
        return;  // Mode not set yet - can't broadcast
    }

    uint8_t packet[Constants::DISCOVERY_PACKET_SIZE];
    buildDiscoveryPacket(packet, discoveryDeviceId, Constants::TCP_PORT, Constants::OTA_HTTP_PORT);

    // Sign discovery packet if signing is enabled (optional/guarded)
    // Falls back to unsigned (zeros) if signing disabled or key not available
    signDiscoveryPacket(packet);

    // Use SDK's broadcastIP() which respects actual subnet mask
    IPAddress broadcastIp = WiFi.broadcastIP();
    udpDiscovery.beginPacket(broadcastIp, Constants::DISCOVERY_PORT);
    udpDiscovery.write(packet, Constants::DISCOVERY_PACKET_SIZE);
    udpDiscovery.endPacket();
}

// ── WiFi State Machine ────────────────────────────────────────────────────────
// Testable pure function: Determine if WiFi retry is needed (testable unit)
static bool shouldRetryWiFi(WiFiState::State state, uint32_t now, uint32_t lastRetry) {
    if (state != WiFiState::State::DISCONNECTED &&
        state != WiFiState::State::CONNECTING &&
        state != WiFiState::State::RECONNECTING) {
        return false;
    }
    return (now - lastRetry) >= Constants::WIFI_CONNECT_RETRY_INTERVAL_MS;
}

static const char* wifiStateName(WiFiState::State state) {
    switch (state) {
        case WiFiState::State::DISCONNECTED: return "DISCONNECTED";
        case WiFiState::State::CONNECTING: return "CONNECTING";
        case WiFiState::State::CONNECTED_STA: return "CONNECTED_STA";
        case WiFiState::State::CONNECTED_AP: return "CONNECTED_AP";
        case WiFiState::State::RECONNECTING: return "RECONNECTING";
        default: return "UNKNOWN";
    }
}

// ── Credential Management (Testable Pure Functions) ────────────────────────────
// Testable pure function: Determine credential source priority
static CredentialSource determineCredentialSource() {
    if (hasStoredWifiCredentials()) {
        return CredentialSource::STORED_NVS;
    }
    if (WIFI_SSID != nullptr) {
        return CredentialSource::BAKED_IN;
    }
    return CredentialSource::NONE;
}

// Testable pure function: Check if AP mode fallback is needed
static bool shouldFallbackToApMode(CredentialSource source, uint32_t connectDurationMs) {
    return (source == CredentialSource::STORED_NVS) &&
           (connectDurationMs > Constants::WIFI_CONNECT_TIMEOUT_MS);
}

// Testable pure function: Check if initial connect timeout reached
static bool isInitialConnectTimeout(uint32_t connectDurationMs) {
    return connectDurationMs > (Constants::WIFI_INITIAL_CONNECT_MAX_RETRIES *
                                Constants::WIFI_CONNECT_RETRY_INTERVAL_MS);
}

// Handler for DISCONNECTED state - determines initial connection strategy
struct DisconnectedStateHandler : public WiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        CredentialSource source = determineCredentialSource();

        switch (source) {
            case CredentialSource::STORED_NVS: {
                String storedSsid, storedPass;
                if (loadWifiCredentials(storedSsid, storedPass)) {
                    Serial.printf("Using stored WiFi credentials: %s\r\n", storedSsid.c_str());
                    WiFi.mode(WIFI_STA);
                    WiFi.setHostname("esp32-can");
                    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
                    ctx.connectStartTime = now;
                    ctx.lastRetryMs = now;
                    return StateTransition(WiFiState::State::CONNECTING);
                }
                break;
            }
            case CredentialSource::BAKED_IN: {
                WiFi.disconnect(false, true);
                WiFi.mode(WIFI_STA);
                WiFi.setHostname("esp32-can");
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                ctx.connectStartTime = now;
                ctx.lastRetryMs = now;
                Serial.printf("Connecting to %s\r\n", WIFI_SSID);
                return StateTransition(WiFiState::State::CONNECTING);
            }
            case CredentialSource::NONE:
            default:
                Serial.printf("No WiFi credentials available, starting AP mode\r\n");
                WiFi.mode(WIFI_AP);
                WiFi.softAP(AP_SSID, AP_PASS);
                const String ip = WiFi.softAPIP().toString();
                Serial.printf("%sAP: %s  IP: %s%s\r\n", PURPLE, AP_SSID, ip.c_str(), NC);
                Serial.printf("%sConnect to AP and use ATSETWIFI command to configure WiFi%s\r\n", YELLOW, NC);
                return StateTransition(WiFiState::State::CONNECTED_AP);
        }

        return StateTransition();  // Stay DISCONNECTED if something failed
    }
};

// Handler for CONNECTING state - monitors connection progress
struct ConnectingStateHandler : public WiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        const wl_status_t status = WiFi.status();
        const uint32_t connectDuration = now - ctx.connectStartTime;

        if (status == WL_CONNECTED) {
            const String ip = WiFi.localIP().toString();
            Serial.printf("%s\nConnected. IP: %s\r\n%s", GREEN, ip.c_str(), NC);
            return StateTransition(WiFiState::State::CONNECTED_STA, true, true);
        }

        if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
            CredentialSource source = determineCredentialSource();

            if (shouldFallbackToApMode(source, connectDuration)) {
                Serial.printf("%sStored WiFi credentials failed, falling back to AP mode%s\r\n", YELLOW, NC);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(AP_SSID, AP_PASS);
                const String ip = WiFi.softAPIP().toString();
                Serial.printf("%sAP: %s  IP: %s%s\r\n", PURPLE, AP_SSID, ip.c_str(), NC);
                Serial.printf("%sConnect to AP and reconfigure WiFi with ATSETWIFI%s\r\n", YELLOW, NC);
                return StateTransition(WiFiState::State::CONNECTED_AP);
            }

            if (shouldRetryWiFi(WiFiState::State::CONNECTING, now, ctx.lastRetryMs)) {
                String storedSsid, storedPass;
                bool hasStored = (source == CredentialSource::STORED_NVS) &&
                                loadWifiCredentials(storedSsid, storedPass);

                if (hasStored) {
                    Serial.printf("%sWiFi connect failed (status=%d), retrying with stored creds...%s\r\n",
                                    YELLOW, static_cast<int>(status), NC);
                } else {
                    Serial.printf("%sWiFi connect failed (status=%d), retrying...%s\r\n",
                                    YELLOW, static_cast<int>(status), NC);
                }

                WiFi.disconnect(false, true);
                if (hasStored) {
                    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
                } else {
                    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                }
                ctx.lastRetryMs = now;
            }
        } else if (isInitialConnectTimeout(connectDuration)) {
            CredentialSource source = determineCredentialSource();

            if (source == CredentialSource::STORED_NVS) {
                Serial.printf("%sStored WiFi credentials timeout, falling back to AP mode%s\r\n", YELLOW, NC);
                WiFi.mode(WIFI_AP);
                WiFi.softAP(AP_SSID, AP_PASS);
                const String ip = WiFi.softAPIP().toString();
                Serial.printf("%sAP: %s  IP: %s%s\r\n", PURPLE, AP_SSID, ip.c_str(), NC);
                return StateTransition(WiFiState::State::CONNECTED_AP);
            } else {
                Serial.printf("%sInitial connect timeout, entering RECONNECTING state%s\r\n", YELLOW, NC);
                return StateTransition(WiFiState::State::RECONNECTING);
            }
        } else if ((now - ctx.lastRetryMs) >= Constants::WIFI_CONNECT_RETRY_INTERVAL_MS) {
            Serial.printf("%s.%s", GREEN, NC);
            ctx.lastRetryMs = now;
        }

        return StateTransition();  // Stay in CONNECTING
    }
};

// Handler for RECONNECTING state - retries indefinitely ("60 years")
struct ReconnectingStateHandler : public WiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        // Check if reconnection succeeded
        if (WiFi.status() == WL_CONNECTED) {
            const String ip = WiFi.localIP().toString();
            Serial.printf("%sWiFi RECONNECTED. IP: %s\r\n%s", GREEN, ip.c_str(), NC);
            return StateTransition(WiFiState::State::CONNECTED_STA, true, true);
        }

        // Retry indefinitely - "60 years, no point in giving up"
        if (shouldRetryWiFi(WiFiState::State::RECONNECTING, now, ctx.lastRetryMs)) {
            const wl_status_t status = WiFi.status();
            Serial.printf("%sWiFi RECONNECTING: status=%d reason=%d, retrying...%s\r\n",
                            YELLOW, static_cast<int>(status),
                            static_cast<int>(ctx.lastDisconnectReason), NC);
            WiFi.disconnect(false, true);

            String storedSsid, storedPass;
            if (hasStoredWifiCredentials() && loadWifiCredentials(storedSsid, storedPass)) {
                WiFi.begin(storedSsid.c_str(), storedPass.c_str());
            } else {
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            }
            ctx.lastRetryMs = now;
        }

        return StateTransition();  // Stay in RECONNECTING
    }
};

// Handler for CONNECTED_STA state - monitors connection health
struct ConnectedStaStateHandler : public WiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        if (WiFi.status() != WL_CONNECTED) {
            return StateTransition(WiFiState::State::RECONNECTING, true, false);
        }
        return StateTransition();  // Stay CONNECTED_STA
    }
};

// Handler for CONNECTED_AP state - AP mode is stable
struct ConnectedApStateHandler : public WiFiStateHandler {
    StateTransition execute(uint32_t now, WiFiState::Context& ctx) override {
        // AP mode stays connected unless explicitly changed - no transitions
        return StateTransition();
    }
};

// ── State Handler Registry (Dispatch Table) ───────────────────────────────────────
static DisconnectedStateHandler disconnectedHandler;
static ConnectingStateHandler connectingHandler;
static ReconnectingStateHandler reconnectingHandler;
static ConnectedStaStateHandler connectedStaHandler;
static ConnectedApStateHandler connectedApHandler;

static WiFiStateHandler* getStateHandler(WiFiState::State state) {
    switch (state) {
        case WiFiState::State::DISCONNECTED: return &disconnectedHandler;
        case WiFiState::State::CONNECTING: return &connectingHandler;
        case WiFiState::State::RECONNECTING: return &reconnectingHandler;
        case WiFiState::State::CONNECTED_STA: return &connectedStaHandler;
        case WiFiState::State::CONNECTED_AP: return &connectedApHandler;
        default: return &disconnectedHandler;  // Fallback
    }
}

// ── State Transition Application ───────────────────────────────────────────────────
static void applyStateTransition(const StateTransition& transition, WiFiState::Context& ctx) {
    if (transition.nextState == WiFiState::State::DISCONNECTED &&
        ctx.state == WiFiState::State::DISCONNECTED) {
        return;  // No transition
    }

    ctx.state = transition.nextState;

    if (transition.setTcpServerRestartFlag) {
        ctx.tcpServerNeedsRestart = true;
    }

    if (transition.initNtp) {
        initNtpSync();
    }
}

static void onWiFiDisconnected(const WiFiEvent_t&, const WiFiEventInfo_t& info) {
    wifiCtx.lastDisconnectReason = static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);

    // Special handling for AUTH_FAIL (202) - make the message more descriptive
    if (wifiCtx.lastDisconnectReason == WIFI_REASON_AUTH_EXPIRE ||
        wifiCtx.lastDisconnectReason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        wifiCtx.lastDisconnectReason == WIFI_REASON_AUTH_FAIL) {
        Serial.printf("\n%sAUTH_FAIL (%d): password rejected for SSID '%s' — check credentials%s\r\n", RED,
                        static_cast<int>(wifiCtx.lastDisconnectReason), WiFi.SSID().c_str(), NC);
    } else {
        Serial.printf("\n%sWiFi disconnected: reason=%d %s%s [%s]\r\n", RED,
                        static_cast<int>(wifiCtx.lastDisconnectReason),
                        WiFi.disconnectReasonName(wifiCtx.lastDisconnectReason), NC, WiFi.SSID().c_str());
    }

    // Transition to RECONNECTING state - never give up
    if (wifiCtx.state == WiFiState::State::CONNECTED_STA) {
        wifiCtx.state = WiFiState::State::RECONNECTING;
        wifiCtx.tcpServerNeedsRestart = true;
        wifiCtx.lastRetryMs = millis();
        Serial.printf("%sEntering WiFi RECONNECTING state (will retry indefinitely)%s\r\n", YELLOW, NC);
    }
}

static void updateWiFiStateMachine() {
    const uint32_t now = millis();

    // Get handler for current state (dispatch table lookup)
    WiFiStateHandler* handler = getStateHandler(wifiCtx.state);

    // Execute state handler and get transition
    StateTransition transition = handler->execute(now, wifiCtx);

    // Apply transition if any
    applyStateTransition(transition, wifiCtx);
}

static void sendPrompt(const char* response) {
    client.printf("%s\r\r>", response);
    client.flush();
}

static void sendSerialPrompt(const char* response) {
    Serial.println(response);
    Serial.flush();
}

// ── AT Command Pattern Implementation ───────────────────────────────────────────────

// Testable pure function: Normalize AT command (testable)
static String normalizeAtCommand(const String& cmd) {
    String normalized = cmd;
    normalized.trim();
    normalized.toUpperCase();
    return normalized;
}

// Testable pure function: Build HELO response (testable)
static String buildHeloResponse() {
    char response[128];
    int len = snprintf(response, sizeof(response),
        "ACK DEVICE=%s FIRMWARE=%s DEVICEID=",
        Constants::DEVICE_NAME, Constants::FIRMWARE_VERSION);
    // Append device ID as hex (16 bytes -> 32 hex chars)
    for (int i = 0; i < 16 && len < (int)sizeof(response) - 3; i++) {
        len += snprintf(response + len, sizeof(response) - len, "%02X", discoveryDeviceId[i]);
    }
    snprintf(response + len, sizeof(response) - len, "\r");
    return String(response);
}

// ATZ - Reset handler
struct AtzCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATZ";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        monitorActive = false;
        return AtCommandResult("ELM327 v2.3");
    }
};

// ATE0/ATE1 - Echo handler
struct AteCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATE0" || normalizedCmd == "ATE1";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult("OK");
    }
};

// ATSP - Protocol handler
struct AtspCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd.startsWith("ATSP");
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult("OK");
    }
};

// ATH0/ATH1 - Headers handler
struct AthCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATH0" || normalizedCmd == "ATH1";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult("OK");
    }
};

// ATCSM0/ATCSM1 - Serial monitor handler
struct AtcsmCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATCSM0" || normalizedCmd == "ATCSM1";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult("OK");
    }
};

// ATMA - Monitor activation handler
struct AtmaCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATMA";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        monitorActive = true;
        return AtCommandResult("OK");
    }
};

// ATPC - Monitor deactivation handler
struct AtpcCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATPC";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        monitorActive = false;
        return AtCommandResult("OK");
    }
};

// ATHELO/HELLO - Device identification handler
struct AtheloCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATHELO" || normalizedCmd == "HELLO";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult(buildHeloResponse().c_str());
    }
};

// Testable pure function: Parse SETWIFI parameters (testable)
static SetWifiParams parseSetWifiParams(const String& params) {
    SetWifiParams result;

    int commaIndex = params.indexOf(',');
    if (commaIndex <= 0) {
        return result;  // Invalid format
    }

    result.ssid = params.substring(0, commaIndex);
    result.password = params.substring(commaIndex + 1);
    result.valid = true;

    return result;
}

// ATSETWIFI - WiFi credentials setter (AUTH-protected)
struct AtsetwifiCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd.startsWith("ATSETWIFI");
    }

    AtCommandResult execute(const String& originalCmd) const override {
        String params = originalCmd.substring(9);  // Skip "ATSETWIFI"
        params.trim();

        SetWifiParams wifiParams = parseSetWifiParams(params);

        if (!wifiParams.valid) {
            Serial.printf("%sSET-WIFI: Invalid format from authenticated client%s\r\n", RED, NC);
            return AtCommandResult("ERROR Invalid format. Use: ATSETWIFI<ssid>,<pass>");
        }

        // Validate SSID length
        if (wifiParams.ssid.length() == 0 || wifiParams.ssid.length() > 32) {
            Serial.printf("%sSET-WIFI: Invalid SSID length from authenticated client%s\r\n", RED, NC);
            return AtCommandResult("ERROR Invalid SSID length (1-32 chars)");
        }

        // Validate password length
        if (wifiParams.password.length() == 0 || wifiParams.password.length() > 64) {
            Serial.printf("%sSET-WIFI: Invalid password length from authenticated client%s\r\n", RED, NC);
            return AtCommandResult("ERROR Invalid password length (1-64 chars)");
        }

        // Store credentials to NVS
        if (storeWifiCredentials(wifiParams.ssid, wifiParams.password)) {
            Serial.printf("%sSET-WIFI: Stored credentials for SSID: %s%s\r\n",
                            GREEN, wifiParams.ssid.c_str(), NC);
            return AtCommandResult("OK WiFi credentials stored. Rebooting to connect...", true, true);
        } else {
            Serial.printf("%sSET-WIFI: NVS storage failed%s\r\n", RED, NC);
            return AtCommandResult("ERROR Failed to store credentials");
        }
    }
};

// ATI - Device info handler
struct AtiCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATI";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult("ESP32 CAN Bridge v0.1");
    }
};

// ATREBOOT - Reboot handler
struct AtrebootCommandHandler : public AtCommandHandler {
    bool matches(const String& normalizedCmd) const override {
        return normalizedCmd == "ATREBOOT";
    }

    AtCommandResult execute(const String& originalCmd) const override {
        return AtCommandResult("REBOOT", true, true);
    }
};

// ── AT Command Registry (Command Pattern + OpenClosed) ──────────────────────────────
// Adding a new command = push_back a new handler (no dispatcher change needed)

static std::vector<AtCommandHandler*> atCommandHandlers;

static void registerAtCommandHandlers() {
    // Only register once
    if (!atCommandHandlers.empty()) return;

    atCommandHandlers.push_back(new AtzCommandHandler());
    atCommandHandlers.push_back(new AteCommandHandler());
    atCommandHandlers.push_back(new AtspCommandHandler());
    atCommandHandlers.push_back(new AthCommandHandler());
    atCommandHandlers.push_back(new AtcsmCommandHandler());
    atCommandHandlers.push_back(new AtmaCommandHandler());
    atCommandHandlers.push_back(new AtpcCommandHandler());
    atCommandHandlers.push_back(new AtheloCommandHandler());
    atCommandHandlers.push_back(new AtsetwifiCommandHandler());
    atCommandHandlers.push_back(new AtiCommandHandler());
    atCommandHandlers.push_back(new AtrebootCommandHandler());
}

// ── AT Command Dispatcher (Flat loop with registry lookup) ───────────────────────────
static void handleATCommand(const String& cmd, void (*sendPromptFn)(const char*)) {
    // Initialize command registry on first call
    registerAtCommandHandlers();

    // Normalize command for matching
    String normalizedCmd = normalizeAtCommand(cmd);

    // Find matching handler (flat loop over registry)
    AtCommandHandler* matchingHandler = nullptr;
    for (auto* handler : atCommandHandlers) {
        if (handler->matches(normalizedCmd)) {
            matchingHandler = handler;
            break;
        }
    }

    // Execute or respond with unknown command
    if (matchingHandler) {
        AtCommandResult result = matchingHandler->execute(cmd);
        sendPromptFn(result.response.c_str());

        // Apply side effects
        if (result.shouldFlushClient) {
            client.flush();
            Serial.println("REBOOT");
            Serial.flush();
        }

        if (result.shouldReboot) {
            delay(Constants::TCP_REBOOT_DELAY_MS);
            ESP.restart();
        }
    } else {
        sendPromptFn("?");
    }
}

static void handleAT(const String& cmd) {
    handleATCommand(cmd, sendPrompt);
}

static void handleSerialAT(const String& cmd) {
    handleATCommand(cmd, sendSerialPrompt);
}

static void drainSerialATCommands() {
    static String serialCmd;

    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (serialCmd.length() > 0) {
                handleSerialAT(serialCmd);
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

static void streamFrame(const twai_message_t& msg) {
    // Always build the frame in the canonical ELM327-ish text format
    // (3-digit zero-padded hex ID, space-separated byte hex) so both
    // vehicle-sim's parseCANFrame and raw serial capture tools work.
    char buf[Constants::CAN_FRAME_BUFFER_SIZE];
    int n = snprintf(buf, sizeof(buf), "%03X", msg.identifier);
    uint8_t len = min(msg.data_length_code, (uint8_t)8);
    for (uint8_t i = 0; i < len; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, " %02X", msg.data[i]);
    }
    snprintf(buf + n, sizeof(buf) - n, "\r");

    // USB serial: always emit, independent of any TCP client.
    Serial.print(buf);

    // WiFi TCP: only when a client is connected and ATMA monitoring is active.
    if (client.connected() && monitorActive) {
        client.print(buf);
    }
}

// ── TCP Server Lifecycle Management ───────────────────────────────────────────
// Testable pure function: Check if TCP server restart is needed (testable unit)
static bool shouldRestartTcpServer(const WiFiState::Context& ctx) {
    return ctx.tcpServerNeedsRestart;
}

static void restartTcpServerIfNeeded() {
    if (shouldRestartTcpServer(wifiCtx)) {
        Serial.printf("%sRestarting TCP server on IP change%s\r\n", YELLOW, NC);
        tcpServer.end();
        tcpServer.begin();
        wifiCtx.tcpServerNeedsRestart = false;

        // Disconnect any existing client since IP changed
        if (client.connected()) {
            client.stop();
            Serial.println("TCP: disconnected client due to IP change");
        }
    }
}

// ── TCP Authentication ───────────────────────────────────────────────────────
// Testable pure function: Compare AUTH token (testable unit)
static bool isValidAuthToken(const String& received) {
    // Expected format: "AUTH <token>"
    const String expected = String("AUTH ") + TCP_AUTH_TOKEN;
    return received.equals(expected);
}

// Factory reset: check if GPIO0 (BOOT button) is held at boot
static bool checkFactoryReset() {
    pinMode(Constants::FACTORY_RESET_PIN, INPUT_PULLUP);

    // Check if button is pressed (low = pressed on GPIO0 with pullup)
    if (digitalRead(Constants::FACTORY_RESET_PIN) == LOW) {
        Serial.printf("%sFactory reset: GPIO0 held at boot, waiting %lums to confirm...%s\r\n",
                        YELLOW, Constants::FACTORY_RESET_HOLD_MS, NC);

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
        clearWifiCredentials();
        return true;
    }
    return false;
}

void setup() {
    Serial.begin(Constants::SERIAL_BAUD);

    // Factory reset check before WiFi init - allows wiping stored credentials
    // Same firmware, no reflash needed. Hold BOOT button (GPIO0) during boot.
    bool factoryReset = checkFactoryReset();

    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

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

    // Initialize WiFi state machine and start connection
    wifiCtx.state = WiFiState::State::DISCONNECTED;
    updateWiFiStateMachine();

    // Initialize discovery backoff timer - reset on boot
    resetDiscoveryBackoff();

    // Start NTP sync for accurate discovery timestamps
    initNtpSync();

    // Start TCP server (will be restarted on WiFi reconnect/IP change)
    tcpServer.begin();
    Serial.printf("TCP listening on port %u\r\n", Constants::TCP_PORT);

    // Initialize device ID from MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    memset(discoveryDeviceId, 0, 16);
    memcpy(discoveryDeviceId, mac, 6);

    // Start UDP discovery
#if VEHICLE_SIM_ENABLE_DISCOVERY
    udpDiscovery.begin(Constants::DISCOVERY_PORT);
    Serial.printf("UDP discovery on port %u\r\n", Constants::DISCOVERY_PORT);
#else
    Serial.println("UDP discovery disabled via VEHICLE_SIM_ENABLE_DISCOVERY=0");
#endif

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
    // Update WiFi state machine - handles reconnection indefinitely
    updateWiFiStateMachine();

    // Restart TCP server if WiFi reconnected with new IP
    restartTcpServerIfNeeded();

    // Service any incoming OTA upload before other work so an update isn't
    // starved by CAN traffic. Non-blocking: handles at most one connection.
#if VEHICLE_SIM_ENABLE_OTA_SERVER
    otaLoop();
#endif

    // Broadcast discovery packet periodically with backoff schedule
    // Always broadcast when no buddy - "welcome a buddy" - continues during reconnect
#if VEHICLE_SIM_ENABLE_DISCOVERY
    uint32_t now = millis();
    uint32_t ageMs = now - discoveryCtx.connectTimeMs;
    uint32_t intervalMs = discoveryIntervalMs(ageMs);

    if (now - lastDiscoveryBroadcast >= intervalMs) {
        lastDiscoveryBroadcast = now;
        broadcastDiscovery();
    }
#endif

    // Accept new TCP connections - with proper error handling and cleanup
    if (!client || !client.connected()) {
        WiFiClient next = tcpServer.accept();
        if (next) {
            // Clean up any existing client
            if (client.connected()) {
                client.stop();
                Serial.println("TCP: replaced existing client");
            }

            client = next;
            monitorActive = false;

            // Require AUTH before any other commands.
            // Client must send "AUTH <token>" as the first line.
            // This prevents unauthorized reboots and captures on the local network.
            client.setTimeout(Constants::TCP_AUTH_TIMEOUT_MS);

            // Read AUTH line with timeout guard
            String firstLine = client.readStringUntil('\r');
            firstLine.trim();

            if (!isValidAuthToken(firstLine)) {
                Serial.printf("%sTCP: rejected unauthenticated connection (received: %s)%s\r\n",
                                RED, firstLine.c_str(), NC);
                client.println("ERROR unauthorized");
                client.flush();
                client.stop();
                client = WiFiClient();  // Ensure client is reset
            } else {
                Serial.printf("%sTCP: client authenticated from %s%s\r\n",
                                GREEN, client.remoteIP().toString().c_str(), NC);
                client.println("OK");
                client.flush();
            }
        }
    }

    const bool haveClient = client && client.connected();

    // Command mode: read AT commands until ATMA switches to monitor.
    // Only relevant when a TCP client is connected.
    if (haveClient && !monitorActive && client.available()) {
        client.setTimeout(Constants::TCP_COMMAND_TIMEOUT_MS);
        String cmd = client.readStringUntil('\r');
        if (cmd.length() > 0) handleAT(cmd);
    }

    // Check if client disconnected - cleanup resources and reset discovery backoff
    if (!haveClient && monitorActive) {
        monitorActive = false;
        resetDiscoveryBackoff();  // Reset backoff timer to welcome a new buddy
    }

    drainSerialATCommands();

    // Always drain the TWAI RX queue — each frame is received once and
    // dispatched to Serial unconditionally, and to TCP only when a client
    // is connected AND monitorActive. This keeps serial logging live even
    // with no WiFi client, and never double-reads a frame.
    const bool suppressSerialFrames = millis() < serialQuietUntilMs;
#if VEHICLE_SIM_ENABLE_TWAI
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        if (!suppressSerialFrames) {
            streamFrame(msg);
        }
    }
#endif
}
