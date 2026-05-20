# Hardware Adapter Guide

## Objective

Real-time vehicle telemetry via BLE to an iOS/macOS app. The adapter must stream CAN or OBD2 data at sufficient rate for a vehicle dynamics twin (10-100 Hz).

---

## Adapter Comparison Matrix

| Adapter | Cost | Protocol | BLE | Tesla M3/Y | Audi e-tron | Toyota Aygo | Drive Mode |
|---------|------|----------|-----|------------|-------------|-------------|------------|
| ELM327 V2.1 clone | $10-30 | OBD2 PIDs | Yes | No (no OBD2) | No (UDS required) | **Yes** | Yes (PID queries) |
| ELM327 V2.1 (ATMA) | $10-30 | Raw CAN | Yes | Unreliable (10% throughput) | No (gateway blocks) | N/A | Unreliable |
| OBDLink CX | ~$100 | OBD2 PIDs | Yes (BLE) | No (no OBD2) | Maybe (limited) | Yes | Yes |
| OBDLink MX+ | ~$150 | OBD2 + CAN | BT Classic | No (no OBD2) | **Maybe** (29-bit CAN) | Yes | Yes |
| OBDeleven | $80-150 | UDS over CAN | Yes (BLE) | N/A | **Yes** (VAG-specific) | No (VAG only) | Yes |
| Carista | $40-80 | UDS over CAN | Yes (BLE) | N/A | **Yes** (VAG-specific) | No (VAG only) | Yes |
| ESP32 + CAN transceiver | $20-30 | Raw CAN | Yes (BLE) | **Yes** (X179 or OBD2) | Maybe (direct CAN) | N/A | **Yes** |
| CANserver (JWardell) | ~$60 | Raw CAN | No (WiFi) | **Yes** (Tesla-specific) | No | No | Yes |
| comma four | ~$999 | Raw CAN + CAN-FD | No (USB) | **Yes** (openpilot) | N/A | N/A | Yes |
| PEAK PCAN-USB | ~$300+ | Raw CAN | No (USB) | **Yes** (with cable) | **Yes** (with cable) | N/A | Yes |

---

## Vehicle-Specific Recommendations

### Toyota Aygo (and similar ICE vehicles)

**Best adapter: ELM327 V2.1 BLE clone ($10-30)**

Standard OBD2 PIDs work perfectly. The Aygo uses ISO 15765-4 CAN (11-bit, 500kbps) with no gateway interference.

| Approach | Works? | Evidence |
|----------|--------|----------|
| OBD2 PID queries (Mode 01) | **Yes** | Adapter's own app reads RPM, battery voltage successfully |
| ELM327 ATMA (raw CAN) | Not tested | Standard OBD2 queries sufficient |
| ATSP0 (auto protocol) | **Yes** | Aygo responds on auto-detected protocol |

**What works**: RPM (0x0C), speed (0x0D), throttle (0x11), engine load (0x04), coolant temp (0x05), battery voltage (0x42)

**What doesn't**: Nothing — standard OBD2 works as expected

### Audi e-tron (MLB Evo)

**No viable path with ELM327.** The e-tron requires UDS protocol access through the gateway ECU.

| Approach | Works? | Evidence |
|----------|--------|----------|
| ELM327 OBD2 PIDs | **No** | All-zero responses, adapter's own app also fails |
| ELM327 ATMA (raw CAN) | **No** | CAN bus silent when parked (gateway blocks) |
| ATSP0 (auto protocol) | **No** | Clone firmware unreliable, wrong protocol anyway |
| OBDeleven | **Yes** | VAG-specific, UDS over CAN, most popular e-tron tool |
| Carista | **Yes** | VAG-specific, coding and live data |
| OBDLink MX+ | **Maybe** | Supports 29-bit CAN, J2534 — may work with VAG software |
| Audi Cloud API | **Yes** | evcc reads SoC, charging via GraphQL API |

**Route to support Audi e-tron:**

1. **OBDeleven ($80-150)** — Most practical. BLE adapter, VAG-specific app, speaks UDS natively. Would need to reverse-engineer or find documentation for the OBDeleven BLE protocol to integrate directly.

2. **OBDLink MX+ ($150)** — Professional adapter supporting all OBD2 protocols including 29-bit CAN. J2534 passthrough. May work with VAG-specific software like VCDS.

3. **Audi Cloud API (free)** — Low update rate (~1 Hz) but gives SoC, range, charging status. No motor torque, no steering. Good for stationary/fleet data.

4. **VCDS / VCX Nano ($200-400)** — Ross-Tech's VAG diagnostic tool. Has documented UDS access to all ECUs. Could potentially use the HEX-NET's WiFi interface for custom integration.

### Tesla Model 3/Y

**We already have the Tesla 26-pin OBD2 harness.** All commercial products use this same harness + an OBD2 dongle in raw CAN monitor mode.

| Approach | Works? | Evidence |
|----------|--------|----------|
| 26-pin harness + ELM327 ATMA | **Needs testing** | ELM327 may not keep up at 500kbps (10% throughput) |
| 26-pin harness + OBDLink CX (BLE) | **Yes** | tesLAX uses this exact setup |
| 26-pin harness + OBDLink MX+ (BT Classic) | **Yes** | Scan My Tesla uses this |
| 26-pin harness + ESP32 + CAN | **Yes** | flipper-tesla-fsd, CANserver, $14-60 |
| ELM327 OBD2 PIDs | **No** | Tesla doesn't implement SAE J1979 |
| Tesla Fleet API (cloud) | **Yes** | Low rate (~1 Hz), no motor/steering data |

**Our hardware**: "Model 3 & Model Y OBD2 Diagnostic Harness Scanner Splitter 26Pin Adapter for TSL Model 3 & Model Y Post Jan 2019 to Now" — plugs into diagnostic connector behind rear console, provides standard OBD2 port, allows reading of CAN data.

**Plan**: Try existing ELM327 clone first in ATMA mode. If throughput is insufficient, upgrade to OBDLink CX (BLE, ~$100) which tesLAX uses successfully.

---

## Tradeoffs

### ELM327 Clone ($10-30)
- **Pro**: Cheap, BLE, works with standard OBD2 vehicles
- **Con**: Unreliable for raw CAN streaming, no UDS support, clone firmware bugs
- **Best for**: Standard OBD2 vehicles (Toyota, Honda, Ford, etc.)

### ESP32 + CAN Transceiver ($20-30)
- **Pro**: Full raw CAN access, BLE/WiFi, reliable at 500kbps, cheap, open-source firmware
- **Con**: Requires custom firmware, no OBD2 PID support, vehicle-specific cables
- **Best for**: Tesla, custom CAN projects, vehicles without standard OBD2

### OBDLink + Tesla Adapter Cable ($100-150)
- **Pro**: BLE, proven by tesLAX/Scan My Tesla in raw CAN monitor mode, commercial quality
- **Con**: ELM327 throughput bottleneck (~460 frames/sec text output), Tesla adapter cable required
- **Best for**: Tesla with consumer-friendly setup (plug and play with existing apps)

### OBDeleven ($80-150)
- **Pro**: VAG-specific UDS access, BLE, proven with e-tron, consumer-friendly
- **Con**: VW/Audi only, proprietary protocol, requires their app
- **Best for**: Audi e-tron, VW group vehicles

### OBDLink MX+ ($150)
- **Pro**: Professional-grade, all OBD2 protocols, J2534, 29-bit CAN
- **Con**: BT Classic (not BLE on all models), expensive
- **Best for**: Multi-protocol support, professional diagnostics

---

## ESP32 + CAN Architecture (Recommended for Tesla)

### Components
- ESP32 dev board (any model) — ~$6
- MCP2515 CAN controller or SN65HVD230 CAN transceiver — ~$2
- X179 pigtail connector (Tesla-specific) — ~$10
- Optional: BNO055 IMU module — ~$2
- **Total: ~$20**

### Data Flow
```
Tesla CAN Bus (500kbps, Party CAN or Bus 6 via X179)
  → MCP2515 CAN controller (SPI to ESP32)
  → ESP32 TWAI driver (hardware CAN controller)
  → BLE GATT server (notify characteristic)
  → iOS/macOS CoreBluetooth
  → vehicle-sim BLEManager (invokeDataCallback)
  → DBCSignalTranslator (DBC signal extraction via Model3CAN.dbc)
  → VehicleSim (telemetry state model)
```

### Frame Format over BLE
Each BLE notification carries one CAN frame:
- Byte 0-1: CAN ID (little-endian, 11-bit)
- Byte 2-9: 8 data bytes

This matches the existing `parseCANFrame` output format in BLEManagerBase.

### IMU Integration (Optional)
An IMU module (BNO055) can be connected to the same ESP32 via I2C. The ESP32 would then send both CAN frames and IMU readings over BLE. However, **motor torque is more useful than accelerometer data** for a vehicle dynamics twin — torque is directly available on CAN ID 0x108.

---

## Multi-Vehicle Architecture

### Transport Abstraction

The vehicle-sim project should support multiple transport types behind a common interface:

```
BLEManagerBase (abstract)
  ├── ELM327Transport    → OBD2 PID queries (Toyota, standard OBD2)
  │   ├── sendASCII()    → Mode 01 PID queries
  │   └── parseOBD2Response() → Decode PID values
  ├── CANTransport       → Raw CAN streaming (Tesla, custom)
  │   ├── sendPromptDrivenSequence() → AT init (if ELM327-based)
  │   └── parseCANFrame() → Extract CAN ID + data bytes
  └── CloudTransport     → HTTP/WebSocket (Audi API, Tesla Fleet API)
      └── processResponse() → Parse JSON/XML
```

### Signal Decoder Abstraction

```
DBCSignalTranslator (generic DBC-driven)
  ├── DBCParser              → Parses DBC files
  ├── DBCSignalMapper        → Translates CAN values
  └── VehicleSignalFactory    → Builds VehicleSignal from extracted values
```

### Connection Points by Vehicle

| Vehicle | Connection | Transport | Decoder |
|---------|------------|-----------|---------|
| Toyota Aygo | OBD2 port (under dash) | ELM327 (OBD2 PIDs) | OBD2SignalTranslator |
| Audi e-tron | OBD2 port (under dash) | OBDeleven (UDS) | DBCSignalTranslator (vw_mlb.dbc) |
| Tesla Model 3/Y | 26-pin harness → OBD2 (behind console) | ELM327 ATMA or OBDLink (raw CAN) | DBCSignalTranslator (Model3CAN.dbc) |
| Generic OBD2 | OBD2 port (under dash) | ELM327 (OBD2 PIDs) | OBD2SignalTranslator |

---

## Sources

- [commaai/opendbc](https://github.com/commaai/opendbc) — DBC files and signal definitions
- [hypery11/flipper-tesla-fsd](https://github.com/hypery11/flipper-tesla-fsd) — Proven ESP32 + CAN setup for Tesla
- [joshwardell/CANserver](https://github.com/joshwardell/CANserver) — ESP32 CAN-to-WiFi reference design
- [Adminius/ESP32-ScanMyTesla](https://github.com/Adminius/ESP32-ScanMyTesla) — ESP32 CAN adapter design
- [evcc-io/evcc](https://github.com/evcc-io/evcc) — Audi cloud API reference
- [OBDeleven](https://obdeleven.com) — VAG-specific diagnostic adapter
- [OBDLink](https://www.scantool.net) — Professional OBD2 adapters
- [CSS Electronics](https://www.csselectronics.com/) — CAN bus tools and documentation
