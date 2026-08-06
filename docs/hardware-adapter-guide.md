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
- ESP32-WROOM-32 dev board — ~$6
- SN65HVD230 (VP230) CAN transceiver — ~$2
- OBD2 breakout cable (pin 6 = green, pin 14 = brown/white) — ~$5
- Optional: LM2596 buck converter (12V→5V from OBD2 pin 16) — ~$1
- **Total: ~$14**

### OBD2 Male Connector Pin Layout (Our Harness)

**IMPORTANT:** Our OBD2 breakout cable uses non-standard wire colours. Use the table below — do not rely on standard OBD2 colour conventions.

| Pin | Wire Colour | Signal | Notes |
|-----|-------------|--------|-------|
| 1 | Black | Vendor-specific | Not used |
| 2 | Brown | Vendor-specific | Not used |
| 3 | Red | Vendor-specific | Not used |
| 4 | **Orange** | **Chassis GND** | **Connect to ESP32 GND + SN65HVD230 GND** |
| 5 | **Yellow** | **Signal GND** | Tie to pin 4 (common GND) |
| 6 | **Green** | **CAN-H** | Connect to SN65HVD230 CANH |
| 7 | Blue | Vendor-specific | Not used |
| 8 | Purple | Vendor-specific | Not used |
| 9 | Gray | Vendor-specific | Not used |
| 10 | White | Vendor-specific | Not used |
| 11 | Pink | Vendor-specific | Not used |
| 12 | Light green | Vendor-specific | Not used |
| 13 | Black and white | Vendor-specific | **NOT CAN-L** — do not use |
| 14 | **Brown and white** | **CAN-L** | Connect to SN65HVD230 CANL |
| 15 | Red and white | Vendor-specific | Not used |
| 16 | **Green and white** | **+12V** | **→ LM2596 buck converter VIN** |

> **Pin 13 trap:** Pin 13 carries a black/white wire that is vendor-specific, **NOT CAN-L**. CAN-L is pin 14 (brown/white). They look similar — wiring CAN-L to the black/white (pin 13) wire gives power-but-no-data.
>
> **Pin 16 is your 12V source:** Green/white wire on pin 16 provides +12V when the vehicle is awake. This powers the LM2596 buck converter.

### Wiring Diagram — CAN + Power (with LM2596 Buck Converter)

```
    ┌──────────────────────────────────────────────────────────────────────┐
    │                        OBD2 MALE PLUG (our harness)                  │
    │                                                                      │
    │   Pin 4: Orange  ──► GND ──────────────────────────────────────────┐ │
    │   Pin 5: Yellow  ──► GND (tie to pin 4)                            │ │
    │   Pin 6: Green   ──► CAN-H ──────────────────────────────────────┐ │ │
    │   Pin 14: Brn/Wht ──► CAN-L ───────────────────────────────────┐ │ │ │
    │   Pin 16: Grn/Wht ──► +12V ──────────────────────────────────┐ │ │ │ │
    │                                                                      │ │ │ │
    └────────────────────────────┬───────────────────────────────────┘ │ │ │ │
                                 │                                     │ │ │ │
                    ┌────────────┴────────────┐            ┌────────────┴──┴──┴──┘
                    │                         │            │
              ┌─────▼─────┐           ┌───────▼────────┐  ┌▼──────────────────┐
              │  SN65HVD  │           │   LM2596       │  │   ESP32           │
              │  230      │           │  Buck Conv.    │  │   WROOM-32        │
              │           │           │                │  │                   │
              │ CANH ●────┼─── Green  │                │  │                   │
              │ CANL ●────┼─── Brn/Wt │                │  │                   │
              │ GND  ●────┼─── Orange │                │  │                   │
              │ VCC  ●────┼───────────┤ VIN ◄─── Grn/Wht│  │                   │
              │           │           │ GND ◄─── Orange │  │                   │
              │           │           │ VOUT (5V) ──────┼──┤ VIN               │
              └───────────┘           └────────────────┘  │                   │
                                                          │ GPIO 21 (TX) ─────┤
                                                          │ GPIO 22 (RX) ─────┤
                                                          │ 3V3 ◄─────────────┤
                                                          │ GND ◄─────────────┤
                                                          └───────────────────┘
```

### LM2596 Buck Converter Wiring Detail

The LM2596 module (typically a blue PCB with a potentiometer) has 4 pins: **VIN, GND, VOUT, GND** (or sometimes VIN, GND, OUT, GND).

| LM2596 Pin | Connect To | Wire Colour (from OBD2) | Notes |
|------------|------------|-------------------------|-------|
| **VIN** | OBD2 Pin 16 | **Green/White** | +12V from vehicle (only live when vehicle awake) |
| **GND** (input side) | OBD2 Pin 4 | **Orange** | Common ground with ESP32 and CAN transceiver |
| **VOUT** (or OUT) | ESP32 **VIN** pin | **Red** (your choice) | Regulated 5V output — **adjust pot to 5.0V before connecting ESP32** |
| **GND** (output side) | ESP32 **GND** | **Orange** | Same ground net |

> **⚠️ Critical: Set output voltage BEFORE connecting to ESP32**
> 1. Connect LM2596 VIN to +12V (OBD2 pin 16, green/white) and GND to ground (OBD2 pin 4, orange)
> 2. Power on vehicle (or apply 12V bench supply)
> 3. Measure VOUT with multimeter
> 4. Adjust onboard potentiometer until VOUT = **5.0V ±0.1V**
> 5. Only then connect VOUT → ESP32 VIN and GND → ESP32 GND
> 
> The ESP32 VIN pin accepts 5V (regulated). Do NOT feed 12V directly to ESP32 VIN — it will damage the onboard regulator.

### Complete Wire Colour Reference (All Signals)

Colours follow the OBD2 harness (the fixed constraint) so each signal keeps one colour across all three boards. This deliberately breaks the usual black=GND convention — go by the table, not by colour convention.

| Wire colour | Signal | SN65HVD230 pin | ESP32 pin | OBD2 pin (harness wire) |
|-------------|--------|---------------|-----------|-------------------------|
| Red         | 3.3V power (from ESP32 3V3) | 3.3V       | 3V3       | —                       |
| **Orange**  | **GND**    | **GND**       | **GND**   | **4 (orange)**          |
| Yellow      | GND (tie to pin 4) | —       | —         | 5 (yellow) — tie to common GND |
| **Green**   | **CAN-H**  | **CANH**      | —         | **6 (green)**           |
| Blue        | TX     | TX            | D22       | —                       |
| Purple      | (unused) | —             | —         | 7 (blue) — not used     |
| Gray        | (unused) | —             | —         | 8 (purple) — not used   |
| White       | (unused) | —             | —         | 9 (gray) — not used     |
| Pink        | (unused) | —             | —         | 10 (white) — not used   |
| Light green | (unused) | —             | —         | 11 (pink) — not used    |
| **Black/White** | Vendor-specific (NOT CAN-L) | — | — | **13 — DO NOT USE FOR CAN** |
| **Brown/White** | **CAN-L**  | **CANL**      | —         | **14 (brown/white)**    |
| Red/White   | (unused) | —             | —         | 15 (red/white) — not used |
| **Green/White** | **+12V**   | —             | **LM2596 VIN** | **16 (green/white)** |

> Both OBD2 GND pins (4 = orange, 5 = yellow) must be tied to the common GND net. Don't leave pin 5 floating.
>
> **Pin 13 trap:** pin 13 carries a black/white wire that is vendor-specific, NOT CAN-L. CAN-L is pin 14 (brown/white). They look similar — wiring CAN-L to the black/white (pin 13) wire gives power-but-no-data.

### Wire Colour Reference

Colours follow the OBD2 harness (the fixed constraint) so each signal keeps one colour across all three boards. This deliberately breaks the usual black=GND convention — go by the table, not by colour convention.

| Wire colour | Signal | SN65HVD230 pin | ESP32 pin | OBD2 pin (harness wire) |
|-------------|--------|---------------|-----------|-------------------------|
| Red         | 3.3V power | 3.3V       | 3V3       | —                       |
| Orange      | GND    | GND           | GND       | 4 (orange)              |
| Yellow      | GND    | —             | —         | 5 (yellow) — tie to common GND |
| Green       | CAN-H  | CANH          | —         | 6 (green)               |
| Black       | CAN-L  | CANL          | —         | 14 (brown/white)        |
| Brown       | TX     | TX            | D22       | —                       |
| Blue        | RX     | RX            | D21       | —                       |

> Both OBD2 GND pins (4 = orange, 5 = yellow) must be tied to the common GND net. Don't leave pin 5 floating.
>
> **Pin 13 trap:** pin 13 carries a black/white wire that is vendor-specific, NOT CAN-L. CAN-L is pin 14 (brown/white). They look similar — wiring CAN-L to the black/white (pin 13) wire gives power-but-no-data.

### Data Flow
```
Vehicle CAN Bus (500kbps, listen-only)
  → SN65HVD230 CAN transceiver (bus level shifting)
  → ESP32 TWAI driver (hardware CAN controller, GPIO 22/21)
  → WiFi TCP server (port 3333, ELM327 protocol)
  → vehicle-sim CLI (--connect <ip>)
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
