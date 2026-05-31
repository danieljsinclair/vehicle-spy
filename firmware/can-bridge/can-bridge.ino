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
#include <driver/twai.h>

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

static WiFiServer tcpServer(TCP_PORT);
static WiFiClient client;
static bool monitorActive = false;

static void sendPrompt(const char* response) {
    client.printf("%s\r\r>", response);
    client.flush();
}

static void handleAT(const String& cmd) {
    String c = cmd;
    c.trim();
    c.toUpperCase();

    if (c == "ATZ") {
        monitorActive = false;
        sendPrompt("ELM327 v2.3");
    } else if (c == "ATE0" || c == "ATE1") {
        sendPrompt("OK");
    } else if (c.startsWith("ATSP")) {
        sendPrompt("OK");
    } else if (c == "ATH1" || c == "ATH0") {
        sendPrompt("OK");
    } else if (c == "ATCSM1" || c == "ATCSM0") {
        sendPrompt("OK");
    } else if (c == "ATMA") {
        monitorActive = true;
        sendPrompt("OK");
    } else if (c == "ATPC") {
        monitorActive = false;
        sendPrompt("OK");
    } else if (c == "ATI") {
        sendPrompt("ESP32 CAN Bridge v0.1");
    } else {
        sendPrompt("?");
    }
}

static void streamFrame(const twai_message_t& msg) {
    if (!client.connected()) {
        monitorActive = false;
        return;
    }

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%03X", msg.identifier);
    uint8_t len = min(msg.data_length_code, (uint8_t)8);
    for (uint8_t i = 0; i < len; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, " %02X", msg.data[i]);
    }
    snprintf(buf + n, sizeof(buf) - n, "\r");
    client.print(buf);
}

static String ipStr;

void setup() {
    Serial.begin(115200);

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
}

void loop() {
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

    if (!client || !client.connected()) return;

    // Command mode: read AT commands until ATMA switches to monitor
    if (!monitorActive && client.available()) {
        client.setTimeout(100);  // fast response — don't wait 1s for more data
        String cmd = client.readStringUntil('\r');
        if (cmd.length() > 0) handleAT(cmd);
    }

    // Monitor mode: drain TWAI RX queue to TCP
    if (monitorActive) {
        twai_message_t msg;
        while (twai_receive(&msg, 0) == ESP_OK) {
            streamFrame(msg);
        }
    }
}
