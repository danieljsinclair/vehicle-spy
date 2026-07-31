# ESP32 CAN Bus Reader Integration Plan

## Context

vehicle-sim reads vehicle telemetry via BLE OBD2 adapters (ELM327). We want a second data source: an ESP32 device reading CAN frames directly from the vehicle bus and exposing them over Wi-Fi so both the CLI and iOS app can connect without Bluetooth.

Two software components:
1. **ESP32 firmware** — reads CAN bus, streams frames over Wi-Fi
2. **vehicle-sim transport** — connects to ESP32 over Wi-Fi, parses CAN frames into telemetry

## Recommended Hardware & Firmware

### Primary: WiCAN by meatPi Electronics

- **Firmware**: https://github.com/meatpiHQ/wican-fw
- **Legacy repo**: https://github.com/meatPiHQ/WiCAN
- **Docs**: https://meatpihq.github.io/wican-fw/
- **Hardware**: https://www.crowdsupply.com/meatpi-electronics

**Why WiCAN:**
- Purpose-built ESP32-C3 OBD2 adapter with production firmware
- Exposes CAN frames over WiFi (TCP/HTTP API) and BLE
- Supports ELM327 v2.3 command set over TCP — can reuse our ELM327Transport entirely
- WiCAN OBD2 ($42) plugs directly into OBD2 port, no wiring required
- WiCAN PRO ($89) adds WiFi + BLE + USB, CAN 2.0A/B up to 1Mbps
- Active development, OTA updates, MQTT support, web config UI
- Programming examples: https://github.com/meatpiHQ/programming_examples/tree/master/CAN

**No custom firmware needed for Phase 1.** WiCAN speaks ELM327 over TCP — our existing `ELM327Transport` class handles parsing. Only the transport layer (BLE → WiFi TCP) changes.

### Secondary: hypery11/flipper-tesla-fsd (ESP32 port)

- **URL**: https://github.com/hypery11/flipper-tesla-fsd
- Full ESP32 CAN reader with WiFi dashboard, PlatformIO builds
- Supports M5Stack ATOM Lite + ATOMIC CAN Base ($14), Lilygo T-CAN485 ($15)
- 37 Tesla CAN handlers with detailed CAN ID docs
- Best as reference or as firmware base for DIY hardware
- Tesla-specific (FSD unlock focused), not pure telemetry reader

### Not recommended

- `joshwardell/CANserver` — MicroPython, unmaintained since Dec 2022
- `limiter121/esp32-obd2` — ESP-IDF SLCAN emulator, no WiFi streaming
- `limiter121/esp32-obd2-emulator` — Archived Jan 2021

## Firmware Architecture Decision

**Phase 1: Custom firmware (path B).**

Complete the god-class thin-veneer decomposition in firmware/vanilla/*.cpp, preserving the unique vehicle-sim requirements:
- Signed discovery (for iOS integration)
- TCP authentication
- Custom AT/HELO handshake
- OTA updates

This builds on the existing firmware skeleton and maintains our differentiated capabilities.

**Phase 2 (future, optional): Stock WiCAN as alternative transport (path A).**

Add WiCAN support as a complementary client-side transport option:
- At the CLIENT layer: switchable via --wifi / discovery (runtime transport selection)
- At the firmware layer: alternatives on a given device (custom firmware OR stock WiCAN)
- Reuse ELM327Transport parser via ELM327-over-WiFi (existing parser)
- Provides an escape hatch for users who prefer stock firmware over custom features

**Phase 3 (future optimisation): Binary protocol firmware.**

If ELM327 ASCII overhead becomes a bottleneck:
- Fork hypery11/flipper-tesla-fsd ESP32 port, strip injection/FSD logic
- Stream binary CAN frames: `[timestamp_ms:4][can_id:2][dlc:1][data:8]` = 15 bytes
- PlatformIO build targeting generic ESP32 + MCP2515 or M5Stack ATOM

## Integration Architecture

```
ESP32 (WiCAN) over WiFi TCP
  → WiFiTransport (new)
  → ELM327Transport::parseCANFrame() (existing)
  → DBCTranslationService::processFrame() (existing)
  → VehicleSignal → display
```

The DBC translation layer is transport-agnostic — `processFrame()` accepts raw bytes and returns `VehicleSignal`. Only the transport layer changes.

## Files to Create

### WiFi Transport Layer

**`include/vehicle-sim/wifi/WiFiTransport.h`**
- TCP socket connection to ESP32 (default port 3333)
- Methods: `connect(ip, port)`, `disconnect()`, `isConnected()`, `sendASCII()`, `readLine()`
- POSIX sockets (no new dependencies)

**`src/wifi/WiFiTransport.cpp`**
- TCP socket lifecycle (connect, read loop, reconnect)
- ELM327 command sending over TCP (same AT commands as BLE path)
- CAN frame line reading from TCP stream
- Connection health monitoring

### CLI Integration

**`include/vehicle-sim/cli/ESP32RunContext.h`**
- Mirrors BLERunContext structure
- `static int run(ipAddress, port, vehicleType, translationService)`

**`src/cli/ESP32RunContext.cpp`**
- WiFi connection lifecycle
- ELM327 init over TCP: `ATZ`, `ATE0`, `ATSP6`, `ATH1`, `ATCSM1`, `ATMA`
- CAN monitor mode over TCP — same as BLE but via WiFiTransport
- Health check loop (same pattern as BLERunContext)

### Signal Source

**`include/vehicle-sim/domain/WiFiSignalSource.h`**
- ISignalSource implementation using WiFiTransport
- Constructor takes WiFiTransport and DBCTranslationService references

**`src/domain/WiFiSignalSource.cpp`**
- WiFi-specific CAN frame accumulation
- Frame parsing via `ELM327Transport::parseCANFrame()`

## Files to Modify

### CLI Options

**`include/vehicle-sim/cli/CliOptions.h`**
- Add `std::string wifi_target` (IP address)
- Add `int wifi_port = 3333`
- Add `[[nodiscard]] bool isWiFi() const`

**`src/cli/CliOptions.cpp`**
- Add `--wifi <ip>` and `--port <port>` options
- Update `validateOptions()` for WiFi
- Update help text

### Main Entry Point

**`src/main.cpp`**
- Add WiFi branch after BLE check:
  ```cpp
  if (opts.isWiFi()) {
      return cli::ESP32RunContext::run(opts.wifi_target, opts.wifi_port,
                                       opts.vehicle_type, translationService);
  }
  ```

### Build System

**`CMakeLists.txt`**
- Add new source files to `VEHICLE_SIM_LIB_SOURCES`

## Hardware Wiring Guide

### Option A: WiCAN OBD2 Adapter (No Wiring)

1. Plug WiCAN into vehicle OBD2 port
2. Connect to WiCAN's WiFi access point (`WiCAN_XXXX`)
3. Configure via web interface (http://192.168.80.1)
4. Set CAN bus speed to 500kbps
5. Enable CAN monitor mode

### Option B: Generic ESP32 + MCP2515 (DIY, ~$15)

Components:
- ESP32 dev board ($4-8)
- MCP2515 CAN controller module ($2-3)
- OBD2 to DuPont cable (pin 6 = CAN-H, pin 14 = CAN-L) ($5)
- 120-ohm termination resistor (most MCP2515 modules have one)

```
MCP2515 Module    ESP32
-------------     --------
VCC               3.3V
GND               GND
CS                GPIO 5
SCK               GPIO 18
MOSI              GPIO 23
MISO              GPIO 19
INT               GPIO 4

MCP2515 CANH      OBD2 pin 6 (CAN-H)
MCP2515 CANL      OBD2 pin 14 (CAN-L)
MCP2515 GND       OBD2 pin 4/5 (ground)
```

### Option C: M5Stack ATOM Lite + ATOMIC CAN Base ($14)

All-in-one option. ATOMIC CAN Base has built-in CAN transceiver. Firmware from hypery11/flipper-tesla-fsd supports this board with PlatformIO target `m5stack-atom`.

## Toolchain Setup

### For WiCAN (no ESP32 toolchain needed)
- WiCAN comes pre-flashed
- Configuration via web interface only

### For custom ESP32 firmware (if needed)

**PlatformIO (recommended):**
1. Install VS Code + PlatformIO IDE extension
2. Clone firmware repo
3. PlatformIO auto-installs ESP-IDF, toolchain, dependencies
4. Build: `pio run -e esp32-mcp2515`
5. Flash: `pio run -t upload`

**ESP-IDF (native):**
1. Install ESP-IDF v5.1+
2. `idf.py build && idy.py -p /dev/ttyUSB0 flash monitor`

## Implementation Phases

## Implementation Roadmap

### Phase 1: Custom Firmware Completion (current work)
- Complete god-class thin-veneer decomposition (firmware/vanilla/*.cpp)
- Preserve unique capabilities: signed discovery, TCP auth, custom AT/HELO, OTA
- Build and flash to ESP32 hardware

### Phase 2: WiFi Transport Layer (CLI, 2-3 days)
- Create `WiFiTransport` with TCP socket connection
- Implement ELM327 command sending over TCP (same AT commands as BLE)
- Reuse `ELM327Transport::parseCANFrame()` for parsing
- Create `WiFiSignalSource` implementing ISignalSource
- Add `--wifi` CLI option and routing in main.cpp
- Create `ESP32RunContext`
- Add tests: mock TCP server → WiFiTransport → frame parsing → VehicleSignal

### Phase 3: iOS App Integration (3-5 days)
- Create iOS WiFi transport using URLSession/Network framework
- Add ESP32 WiFi source option in vehicle selection UI
- Create WiFiSignalSource in VehicleSimWrapper

### Phase 4: Auto-Discovery (2-3 days)
- mDNS/Bonjour discovery for ESP32 on local network
- `--wifi auto` CLI option
- Connection health monitoring, auto-reconnect
- Configuration UI for CAN bus speed, protocol selection

### Phase 5 (optional): Stock WiCAN Support
- Add WiCAN as alternative client-side transport (path A escape hatch)
- Runtime transport selection via --wifi / discovery
- For users who prefer stock firmware over custom features

## Testing Strategy

- **Unit tests**: WiFiTransport (mock TCP), WiFiSignalSource (known CAN frames)
- **Integration test**: mock TCP server → WiFiTransport → DBCTranslationService → VehicleSignal
- **Hardware test**: `vehicle-sim --wifi 192.168.80.1 --vehicle tesla` with WiCAN on vehicle
