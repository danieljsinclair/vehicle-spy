// WifiBench — minimal ESP32 throughput/latency bench sketch (experiments only).
//
// Connects to WiFi (creds injected via -DESP32_WIFI_SSID / -DESP32_WIFI_PASS at
// compile time, in-memory only), opens ONE TCP server on port 3333, and serves a
// single client in one of two modes selected by the client's first line:
//
//   FLOOD   -> spam 32-byte datagrams as fast as LwIP+WiFi allow (measures the
//              ABSOLUTE TCP throughput on THIS link; host counts bytes).
//   ECHO    -> echo every received line verbatim. The host timestamps each frame
//              with its OWN monotonic clock (round-trip RTT) so latency needs no
//              clock sync between devices — the correct methodology.
//
// Mirrors production can-bridge.ino: setNoDelay(true) on the accepted client,
// flush per frame. Single connection slot (like the bridge).
//
// Built/flashed via experiments/flash_wifi_bench.sh — never committed as a binary.

#ifndef ESP32_WIFI_SSID
#define ESP32_WIFI_SSID "manht2"
#endif
#ifndef ESP32_WIFI_PASS
#define ESP32_WIFI_PASS ""
#endif

// Stringize so -D ESP32_WIFI_SSID=manht2 (unquoted) still becomes a string literal.
#define _STR1(x) #x
#define _STR(x)  _STR1(x)
static const char* BENCH_SSID = _STR(ESP32_WIFI_SSID);
static const char* BENCH_PASS = _STR(ESP32_WIFI_PASS);

#include <WiFi.h>

static const uint16_t TCP_PORT = 3333;
static const uint32_t SERIAL_BAUD = 115200;

WiFiServer g_server(TCP_PORT);
WiFiClient g_client;
enum class Mode { NONE, FLOOD, ECHO };
Mode g_mode = Mode::NONE;
uint32_t g_floodSeq = 0;

void setup() {
  Serial.begin(SERIAL_BAUD);
  WiFi.setSleep(false);  // same latency fix as production
  WiFi.mode(WIFI_STA);
  WiFi.begin(BENCH_SSID, BENCH_PASS);
  Serial.printf("[bench] connecting to %s ...\r\n", BENCH_SSID);
  uint32_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 60) {
    delay(500);
    tries++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[bench] WiFi FAILED — staying in boot loop\r\n");
    while (true) delay(1000);
  }
  Serial.printf("[bench] WiFi up: %s\r\n", WiFi.localIP().toString().c_str());
  g_server.begin();
  Serial.printf("[bench] TCP listening on %u\r\n", TCP_PORT);
}

void loop() {
  if (!g_client || !g_client.connected()) {
    g_client = g_server.available();
    if (g_client) {
      g_client.setNoDelay(true);
      g_mode = Mode::NONE;
      g_floodSeq = 0;
      Serial.printf("[bench] client connected\r\n");
    }
    return;
  }

  if (g_mode == Mode::NONE) {
    if (g_client.available()) {
      String line = g_client.readStringUntil('\n');
      line.trim();
      if (line == "FLOOD") g_mode = Mode::FLOOD;
      else if (line == "ECHO") g_mode = Mode::ECHO;
      else g_mode = Mode::ECHO;  // default
      Serial.printf("[bench] mode=%s\r\n",
                    g_mode == Mode::FLOOD ? "FLOOD" : "ECHO");
    }
    return;
  }

  if (g_mode == Mode::FLOOD) {
    // 32-byte datagram: 'D' + 9-digit seq + space + 19 pad bytes + CRLF.
    char buf[40];
    snprintf(buf, sizeof(buf), "D%09u xxxxxxxxxxxxxxxxxxx\r", g_floodSeq);
    g_client.print(buf);
    g_client.flush();
    g_floodSeq++;
    return;  // as tight as possible (self-throttled to UART/RF limit)
  }

  // ECHO mode: forward every complete line back immediately. Use a non-blocking
  // buffered read (one byte at a time) so we don't stall on readStringUntil's
  // internal buffering at high Hz — keeps the echo loop off the critical path.
  static String echoBuf;
  while (g_client.available()) {
    int c = g_client.read();
    if (c < 0) break;
    if (c == '\n') {
      echoBuf.trim();
      if (echoBuf.length() > 0) {
        g_client.print(echoBuf);
        g_client.print('\r');
        g_client.flush();
        echoBuf = "";
      }
    } else if (c != '\r') {
      echoBuf += (char)c;
    }
  }
}
