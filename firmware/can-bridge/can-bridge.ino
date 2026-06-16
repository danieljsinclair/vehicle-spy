// ESP32 CAN-to-WiFi TCP Bridge
// Streams vehicle CAN frames over WiFi to vehicle-sim CLI
//
// Hardware: ESP32-WROOM-32 + SN65HVD230 CAN transceiver
// Wiring:   GPIO 22 → transceiver TX, GPIO 21 → transceiver RX
//           Transceiver CANH → OBD2 pin 6, CANL → OBD2 pin 14
//
// WiFi:     Station mode if ESP32_WIFI_SSID/ESP32_WIFI_PASSWORD defined at build time
//           Falls back to AP mode (ESP32-CAN / cancan12) if not set
// TCP:      port 3333
// Protocol: Minimal ELM327 — ATZ, ATE0, ATSP6, ATH1, ATMA

#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/twai.h>

// OTA update entry points (implemented in ota_update.ino, part of this sketch).
//   otaMarkValidOnBoot - mark the running app valid to cancel an OTA rollback
//   otaSetup           - start the signed-image OTA server (port 3334)
//   otaLoop            - service incoming OTA connections each loop tick
void otaMarkValidOnBoot();
void otaSetup();
void otaLoop();

// WiFi credentials injected via compiler defines (never stored on disk)
// Build with: make flash ESP32_WIFI_SSID=X ESP32_WIFI_PASSWORD=Y
#ifdef ESP32_WIFI_SSID
#define _STRINGIZE(x) #x
#define STRINGIZE(x) _STRINGIZE(x)
static constexpr const char* WIFI_SSID = STRINGIZE(ESP32_WIFI_SSID);
static constexpr const char* WIFI_PASSWORD = STRINGIZE(ESP32_WIFI_PASSWORD);
#else
static constexpr const char* WIFI_SSID = nullptr;
static constexpr const char* WIFI_PASSWORD = nullptr;
#endif

static constexpr const char* AP_SSID = "ESP32-CAN";
static constexpr const char* AP_PASS = "cancan12";

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

// ── UDP Discovery Broadcast ──────────────────────────────────────────────
// Broadcasts signed discovery packets on UDP port 3335 so that CLI and iOS
// apps can auto-discover this ESP32 without manual IP configuration.
static constexpr uint16_t DISCOVERY_PORT = 3335;
static constexpr uint32_t DISCOVERY_INTERVAL_MS = 2000;  // broadcast every 2s
static uint32_t lastDiscoveryBroadcast = 0;
static WiFiUDP udpDiscovery;

// Device ID (first 16 bytes of MAC address, hex)
static uint8_t discoveryDeviceId[16];

// Broadcast a signed discovery packet
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
    // OTA port (2 bytes, big-endian)
    packet[40] = (3334 >> 8) & 0xFF;
    packet[41] = 3334 & 0xFF;

    // For now, zero the signature (unsigned discovery)
    // TODO: sign with Ed25519 once key management is added
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
        const char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (serialCmd.length() > 0) {
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

void setup() {
    Serial.begin(SERIAL_BAUD);

    // TWAI init — listen-only so we never transmit on the vehicle bus
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

    // WiFi — Station mode if config exists, otherwise AP fallback
    if (WIFI_SSID != nullptr) {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("esp32-can");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.printf("Connecting to %s", WIFI_SSID);
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 40) {
            delay(500);
            Serial.print(".");
            retries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            ipStr = WiFi.localIP().toString();
            Serial.printf("\nConnected. IP: %s\n", ipStr.c_str());
        } else {
            Serial.println("\nWiFi failed, falling back to AP mode");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID, AP_PASS);
            ipStr = WiFi.softAPIP().toString();
            Serial.printf("AP: %s  IP: %s\n", AP_SSID, ipStr.c_str());
        }
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        ipStr = WiFi.softAPIP().toString();
        Serial.printf("AP: %s  IP: %s\n", AP_SSID, ipStr.c_str());
    }

    tcpServer.begin();
    Serial.printf("TCP listening on port %u\n", TCP_PORT);

    // Initialize device ID from MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // MAC is 6 bytes, pad to 16 with "ESP32-" prefix
    memset(discoveryDeviceId, 0, 16);
    memcpy(discoveryDeviceId, mac, 6);

    // Start UDP discovery
    udpDiscovery.begin(DISCOVERY_PORT);
    Serial.printf("UDP discovery on port %u\n", DISCOVERY_PORT);

    // OTA: first mark THIS firmware's boot as healthy (cancels any pending
    // rollback from a previous OTA), then start the signed-image OTA server.
    // Order matters — mark-valid before bringing up the server so a rollback
    // condition is cleared before accepting new uploads.
    otaMarkValidOnBoot();
    otaSetup();
}

void loop() {
    // Service any incoming OTA upload before other work so an update isn't
    // starved by CAN traffic. Non-blocking: handles at most one connection.
    otaLoop();

    // Broadcast discovery packet periodically
    uint32_t now = millis();
    if (now - lastDiscoveryBroadcast >= DISCOVERY_INTERVAL_MS) {
        lastDiscoveryBroadcast = now;
        broadcastDiscovery();
    }

    // Accept new TCP connections
    if (!client || !client.connected()) {
        WiFiClient next = tcpServer.accept();
        if (next) {
            if (client) client.stop();
            client = next;
            monitorActive = false;
            sendPrompt("ELM327 v2.3");
            Serial.println("Client connected");
        }
    }

    const bool haveClient = client && client.connected();

    // Command mode: read AT commands until ATMA switches to monitor.
    // Only relevant when a TCP client is connected.
    if (haveClient && !monitorActive && client.available()) {
        client.setTimeout(100);  // fast response — don't wait 1s for more data
        String cmd = client.readStringUntil('\r');
        if (cmd.length() > 0) handleAT(cmd);
    }

    drainSerialATCommands();

    // Always drain the TWAI RX queue — each frame is received once and
    // dispatched to Serial unconditionally, and to TCP only when a client
    // is connected AND monitorActive. This keeps serial logging live even
    // with no WiFi client, and never double-reads a frame.
    const bool suppressSerialFrames = millis() < serialQuietUntilMs;
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        if (!suppressSerialFrames) {
            streamFrame(msg);
        }
    }
}
