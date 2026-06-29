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
#include <driver/twai.h>
#include <string_view>
#include <array>

// TCP auth token — injected at build time, never stored on disk
#ifndef TCP_AUTH_TOKEN
#define TCP_AUTH_TOKEN "vehicle-sim-2026"
#endif

// ANSI color codes for serial output
constexpr std::string_view RED    = "\033[0;31m";
constexpr std::string_view GREEN  = "\033[0;32m";
constexpr std::string_view PURPLE = "\033[0;35m";
constexpr std::string_view NC     = "\033[0m";

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

constexpr std::string_view AP_SSID = "ESP32-CAN";
constexpr std::string_view AP_PASS = "cancan12";

static constexpr gpio_num_t TWAI_TX = GPIO_NUM_22;
static constexpr gpio_num_t TWAI_RX = GPIO_NUM_21;
static constexpr uint32_t TCP_PORT = 3333;

// USB serial baud. Throughput ceiling: at 115200 baud a ~26-char CAN frame
// takes ~2.3ms to transmit, so a very busy bus (>~400 frames/sec) can drop
// frames over serial. The WiFi TCP path is lossless and preferred for max
// fidelity. If serial frame loss is observed, raise SERIAL_BAUD here AND the
// matching `screen` baud in the Makefile firmware-monitor/flash targets so
// both ends agree.
static constexpr uint32_t SERIAL_BAUD = 115200;

static WiFiServer tcpServer(TCP_PORT);
static WiFiClient client;
static bool monitorActive = false;
static uint32_t serialQuietUntilMs = 0;
static wifi_err_reason_t lastDisconnectReason = WIFI_REASON_UNSPECIFIED;

// ── UDP Discovery Broadcast ──────────────────────────────────────────────
// Broadcasts unsigned discovery packets on UDP port 3335 so that CLI and iOS
// apps can auto-discover this ESP32 without manual IP configuration.
static constexpr uint16_t DISCOVERY_PORT = 3335;
static constexpr uint32_t DISCOVERY_INTERVAL_MS = 2000;  // broadcast every 2s
static uint32_t lastDiscoveryBroadcast = 0;
static WiFiUDP udpDiscovery;

// Device ID (first 16 bytes of MAC address, hex)
static uint8_t discoveryDeviceId[16];

// Broadcast a discovery packet. The packet reserves the same signature field as
// the CLI/iOS wire format; current firmware broadcasts unsigned discovery so
// first connection remains usable before OTA keys are flashed.
static void broadcastDiscovery() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Build the 42-byte header
    uint8_t packet[106];  // 42 header + 64 signature

    // Magic: "VSIM"
    packet[0] = 0x56; packet[1] = 0x53; packet[2] = 0x49; packet[3] = 0x4D;
    // Version
    packet[4] = 1;
    // Packet type: 1 = discovery
    packet[5] = 1;
    // Device ID (16 bytes)
    memcpy(packet + 6, discoveryDeviceId, 16);
    // Nonce (8 bytes) — use millis as simple nonce
    uint32_t nonceVal = millis();
    memcpy(packet + 22, &nonceVal, 4);
    memset(packet + 26, 0, 4);
    // Timestamp (8 bytes, big-endian Unix epoch)
    uint64_t now = time(nullptr);
    for (int i = 7; i >= 0; --i) {
        packet[30 + i] = (uint8_t)(now & 0xFF);
        now >>= 8;
    }
    // CAN port (2 bytes, big-endian)
    packet[38] = (TCP_PORT >> 8) & 0xFF;
    packet[39] = TCP_PORT & 0xFF;
    // OTA port (2 bytes, big-endian) — HTTPUpdateServer listens on port 80
    packet[40] = (80 >> 8) & 0xFF;
    packet[41] = 80 & 0xFF;

    // Signature bytes remain zeroed for unsigned discovery broadcasts.
    memset(packet + 42, 0, 64);

    // Broadcast on the local network
    IPAddress broadcastIp = WiFi.localIP();
    broadcastIp[3] = 255;  // 255.255.255.0 subnet → x.x.x.255
    udpDiscovery.beginPacket(broadcastIp, DISCOVERY_PORT);
    udpDiscovery.write(packet, 106);
    udpDiscovery.endPacket();
}

static void sendPrompt(const char* response) {
    client.printf("%s\r\r>", response);
    client.flush();
}

static void sendSerialPrompt(const char* response) {
    Serial.println(response);
    Serial.flush();
}

static void handleATCommand(const String& cmd, void (*sendPromptFn)(const char*)) {
    String c = cmd;
    c.trim();
    c.toUpperCase();

    if (c == "ATZ") {
        monitorActive = false;
        sendPromptFn("ELM327 v2.3");
    } else if (c == "ATE0" || c == "ATE1") {
        sendPromptFn("OK");
    } else if (c.startsWith("ATSP")) {
        sendPromptFn("OK");
    } else if (c == "ATH1" || c == "ATH0") {
        sendPromptFn("OK");
    } else if (c == "ATCSM1" || c == "ATCSM0") {
        sendPromptFn("OK");
    } else if (c == "ATMA") {
        monitorActive = true;
        sendPromptFn("OK");
    } else if (c == "ATPC") {
        monitorActive = false;
        sendPromptFn("OK");
    } else if (c == "ATI") {
        sendPromptFn("ESP32 CAN Bridge v0.1");
    } else if (c == "ATREBOOT") {
        sendPromptFn("REBOOT");
        client.flush();
        Serial.println("REBOOT");
        Serial.flush();
        delay(250);
        ESP.restart();
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
        const char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (!serialCmd.isEmpty()) {
                handleSerialAT(serialCmd);
                serialQuietUntilMs = millis() + 250;
                serialCmd = "";
            }
        } else {
            serialCmd += c;
            if (serialCmd.length() > 64) {
                serialCmd = "";
            }
        }
    }
}

static void streamFrame(const twai_message_t& msg) {
    // Always build the frame in the canonical ELM327-ish text format
    // (3-digit zero-padded hex ID, space-separated byte hex) so both
    // vehicle-sim's parseCANFrame and raw serial capture tools work.
    char buf[32];
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

static String ipStr;

static void onWiFiDisconnected(const WiFiEvent_t&, const WiFiEventInfo_t& info) {
    lastDisconnectReason = static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
    Serial.printf("\n%sWiFi disconnected: reason=%d %s%s [%s]\r\n", RED,
                    static_cast<int>(lastDisconnectReason),
                    WiFi.disconnectReasonName(lastDisconnectReason), NC, WiFi.SSID().c_str());
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    // TWAI init — listen-only so we never transmit on the vehicle bus
#if VEHICLE_SIM_ENABLE_TWAI
    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX, TWAI_RX, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&gcfg, &tcfg, &fcfg) != ESP_OK) {
        Serial.println("FAIL: twai_driver_install");
        while (true) delay(1000);
    }
    if (twai_start() != ESP_OK) {
        Serial.println("FAIL: twai_start");
        while (true) delay(1000);
    }
    Serial.println("TWAI started @ 500kbps (listen-only)");
#else
    Serial.println("TWAI disabled via VEHICLE_SIM_ENABLE_TWAI=0");
#endif

    // WiFi — Station mode if config exists, otherwise AP fallback
    if (WIFI_SSID != nullptr) {
        WiFi.disconnect(false, true);   // erase stale NVS creds before fresh connect
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("esp32-can");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.printf("Connecting to %s", WIFI_SSID);
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 120) {
            delay(500);
            Serial.printf("%s status=%d%s", GREEN, static_cast<int>(WiFi.status()), NC);
            retries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            ipStr = WiFi.localIP().toString();
            Serial.printf("%s\nConnected. IP: %s\r\n%s", GREEN, ipStr.c_str(), NC);
        } else {
            const wl_status_t status = WiFi.status();
            Serial.printf("%s\nWiFi failed: status=%d, falling back to AP mode\n%s", RED, static_cast<int>(status), NC);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID, AP_PASS);
            ipStr = WiFi.softAPIP().toString();
            Serial.printf("%sAP: %s  IP: %s%s\r\n", PURPLE, AP_SSID, ipStr.c_str(), NC);
        }
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        ipStr = WiFi.softAPIP().toString();
        Serial.printf("AP: %s  IP: %s\r\n", AP_SSID, ipStr.c_str());
    }

    tcpServer.begin();
    Serial.printf("TCP listening on port %u\r\n", TCP_PORT);

    // Initialize device ID from MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    // MAC is 6 bytes, pad to 16 with "ESP32-" prefix
    memset(discoveryDeviceId, 0, 16);
    memcpy(discoveryDeviceId, mac, 6);

    // Start UDP discovery
#if VEHICLE_SIM_ENABLE_DISCOVERY
    udpDiscovery.begin(DISCOVERY_PORT);
    Serial.printf("UDP discovery on port %u\r\n", DISCOVERY_PORT);
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
    // Service any incoming OTA upload before other work so an update isn't
    // starved by CAN traffic. Non-blocking: handles at most one connection.
#if VEHICLE_SIM_ENABLE_OTA_SERVER
    otaLoop();
#endif

    // Broadcast discovery packet periodically
#if VEHICLE_SIM_ENABLE_DISCOVERY
    uint32_t now = millis();
    if (now - lastDiscoveryBroadcast >= DISCOVERY_INTERVAL_MS) {
        lastDiscoveryBroadcast = now;
        broadcastDiscovery();
    }
#endif

    // Accept new TCP connections
    if (!client || !client.connected()) {
        WiFiClient next = tcpServer.accept();
        if (next) {
            if (client) client.stop();
            client = next;
            monitorActive = false;
            // Require AUTH before any other commands.
            // Client must send "AUTH <token>" as the first line.
            // This prevents unauthorized reboots and captures on the local network.
            client.setTimeout(5000);
            String firstLine = client.readStringUntil('\r');
            firstLine.trim();
            if (firstLine != "AUTH " TCP_AUTH_TOKEN) {
                client.println("ERROR unauthorized");
                client.stop();
                client = WiFiClient();
                Serial.println("TCP: rejected unauthenticated connection");
            } else {
                client.println("OK");
                Serial.println("TCP: client authenticated");
            }
        }
    }

    const bool haveClient = client && client.connected();

    // Command mode: read AT commands until ATMA switches to monitor.
    // Only relevant when a TCP client is connected.
    if (haveClient && !monitorActive && client.available()) {
        client.setTimeout(100);  // fast response — don't wait 1s for more data
        String cmd = client.readStringUntil('\r');
        if (!cmd.isEmpty()) handleAT(cmd);
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
