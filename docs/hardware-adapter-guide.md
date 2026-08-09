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

---

## Amplifier Power (XH-M542 / TPA3116D2 100W mono)

The ESP32 + CAN bridge (above) draws ~150-200mA and runs off the OBD2 pin-16 12V via the LM2596. The **audio amplifier is a separate, high-current load** and must NOT share the thin OBD2 harness wires.

### Amp current budget

XH-M542 = Texas Instruments TPA3116D2, bridged mono, rated 100W, >90% efficient.

| Condition | 12V input draw | Source |
|-----------|----------------|--------|
| Rated spec | 3A | Vendor listing |
| 100W into 4Ω, full output | ~6-8A | calc (P/η/V) |
| Music with bass peaks into 2Ω | up to ~12A+ | calc — worst case |
| 20V (DeWalt drill battery) | ~2-4A for same power | lower current at higher V |

**Use a 10A inline fuse** minimum; for a 2Ω sub, the rear 12V socket's 12A continuous rating may be marginal — prefer the 26-pin vehicle-end tap (below) or a fused battery tap with a relay.

### Recommended source: Rear 12V socket (cigarette lighter) feed

- Per Tesla owner's manual + TeslaTap: the socket is rated **12A continuous, 16A peak**, fused by Tesla.
- Co-located with the speaker in the boot → short, clean power run.
- Tap the **feed wire behind the panel**, not the socket itself. Splice in a **10A blade fuse holder** between the socket feed and the amp, then run to the amp VIN.

#### Sleep / power-down behaviour (community consensus)

| Car state | 12V socket | Evidence |
|-----------|------------|----------|
| Normal deep sleep (locked, idle 15-30 min) | **OFF (0V)** | Reddit r/TeslaModel3, TMC — "outlet shuts off when car sleeps" |
| Sentry Mode on | ON (car kept awake) | Reddit r/TeslaModel3 |
| Camp Mode / Dog Mode | ON (by design) | TMC camp-mode threads |
| Charging (plugged, not actively charging) | Car may sleep → OFF | Reddit r/TeslaModel3 |
| Software 2024.32+ | Sockets disabled when vehicle off/unoccupied unless Camp Mode/Sentry | evmagz.com, notateslaapp.com |

> **Historically** (pre-2024.32) some builds left the socket always-on; a later update switched it off on exit; community complaints brought back "Accessory Power" (Camp/Sentry or occupied). Net: **under normal sleep it powers down** — exactly what we want (no always-hot drain of the small Tesla 12V battery).
>
> **⚠️ Physical verification required:** multimeter the socket with the car fully asleep (doors locked, 15+ min). It must read 0V. If it reads 12V asleep, the socket is always-hot on your build/firmware and fails the "not always-hot" requirement — fall back to the 26-pin vehicle-end tap (awake-only by design).

### Alternative: 26-pin diagnostic connector, vehicle-end 12V (awake-only)

The 26-pin **vehicle** connector (behind rear console) carries proper vehicle-grade, thick-gauge 12V (pin 15) and GND (pin 26). These are NOT the thin OBD2-harness wires. Our inline harness's vehicle-end wires are the full-CSA type — tapping there is electrically sound and is awake-only by design.

- **Downside:** this 12V is the same electronically-fused diagnostic circuit Tesla monitors. An 8A amp load shares that e-fuse and may trip it or flag a diagnostic fault. Unknown exact rating → not guaranteed.
- **Use only if** the rear socket proves always-hot on your car.

### Rejected: direct 12V battery / rear-seat red cable

Always-hot. On a Tesla the 12V battery is small — an always-on amp can discharge it, the car won't wake, and that is the real "game over". Explicitly avoided: we require awake-only power.

### Grounding the amp

Keep the amp's high-current ground **separate at the splice** but tie it to the same common GND star point as the ESP32/signal ground. Use the boot-panel ground point or run back to the common ground node already built. Do not loop the amp's return current through the thin OBD2 signal ground wires.

### Wiring summary

```
Rear 12V socket feed (behind panel)
   ──► [10A inline blade fuse] ──► XH-M542 VIN
XH-M542 GND ──► boot ground point ──► common GND star (shared with ESP32)
```

> Before calling it permanent: verify socket is 0V asleep (above), add the 10A fuse, and confirm amp draw under real load doesn't exceed 12A (derate for 2Ω speakers).

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
