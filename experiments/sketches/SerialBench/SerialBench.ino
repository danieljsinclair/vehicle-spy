// SerialBench — minimal ESP32 USB/serial throughput bench sketch (experiments).
//
// Emits synthetic CAN-style frames over the USB CDC serial port at 115200 baud
// so the macOS host can measure the REAL serial delivery ceiling (frames/sec)
// and the per-frame delivery latency at 20Hz / 100Hz demand.
//
// Frame text format mirrors the production ELM327-ish format used by CanBridge
// (3-hex ID + space + hex data bytes + CR). ~20 bytes incl delimiter:
//
//   Commands (received from host over serial):
//     "G"        -> BURST: emit as fast as the 115200 UART will accept (ceiling).
//     "R <hz>"   -> RATE: emit exactly <hz> frames/sec (timer-driven).
//     "S"        -> STOP.
//
// Built/flashed via experiments/flash_serial_bench.sh — never committed as binary.

#include <Arduino.h>

static const uint32_t SERIAL_BAUD = 115200;

enum class Mode { IDLE, BURST, RATE };
Mode g_mode = Mode::IDLE;
uint32_t g_rateHz = 0;
uint32_t g_lastEmitMs = 0;
uint32_t g_seq = 0;

void emitFrame() {
  // ~20 bytes: "1F4 11 22 33 44 55 66 77 88 \r"
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "1F4 %02X%02X%02X%02X seq=%u\r",
                   (g_seq >> 24) & 0xFF, (g_seq >> 16) & 0xFF,
                   (g_seq >> 8) & 0xFF, g_seq & 0xFF, g_seq);
  Serial.print(buf);
  g_seq++;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.printf("SERIALBENCH READY baud=%u\r\n", SERIAL_BAUD);
}

void loop() {
  // Drain host commands.
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "G") {
      g_mode = Mode::BURST;
      g_seq = 0;
      Serial.printf("MODE BURST\r\n");
    } else if (line.startsWith("R ")) {
      int hz = line.substring(2).toInt();
      if (hz > 0) {
        g_rateHz = (uint32_t)hz;
        g_mode = Mode::RATE;
        g_lastEmitMs = millis();
        g_seq = 0;
        Serial.printf("MODE RATE %u\r\n", g_rateHz);
      }
    } else if (line == "S") {
      g_mode = Mode::IDLE;
      Serial.printf("MODE IDLE\r\n");
    }
  }

  if (g_mode == Mode::BURST) {
    // Tight loop; Serial.print blocks when the TX FIFO is full, so this
    // self-throttles to the real 115200 UART ceiling. This IS the measurement.
    emitFrame();
  } else if (g_mode == Mode::RATE) {
    uint32_t now = millis();
    uint32_t interval = 1000 / g_rateHz;
    if (now - g_lastEmitMs >= interval) {
      g_lastEmitMs = now;
      emitFrame();
    }
  }
}
