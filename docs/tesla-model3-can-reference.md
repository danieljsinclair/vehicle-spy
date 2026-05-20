# Tesla Model 3/Y CAN Bus & Signal Reference

## Executive Summary

Tesla Model 3/Y does **NOT** implement standard OBD2 PIDs (SAE J1979) and does **NOT have a standard OBD2 port**. The vehicle has a proprietary diagnostic connector (X179, behind rear center console). All telemetry is broadcast as raw CAN frames. A Tesla-specific adapter cable converts the X179 connector to a standard 16-pin OBD2 female socket, then an OBD2 dongle (OBDLink) plugs into that — but it operates in raw CAN monitor mode, not OBD2 PID query mode.

**Confirmed working while driving** by multiple commercial products (Scan My Tesla, tesLAX, S3XY Commander, flipper-tesla-fsd). Accelerometer/IMU data is **NOT** available on any CAN bus — devices use the phone's IMU or a separate sensor.

**Key constraint**: The OBD2 port's Party CAN bus **may go silent in Drive on some firmware builds**. The X179 connector (Bus 6, gateway-mixed forwarding) is the reliable access point. Highland/Juniper (2024+) vehicles switched to DoIP (Ethernet) — MUST use X179 directly for CAN access.

---

## Objectives

### What We Need for a Vehicle Telemetry Twin

| Signal | Required | Available on Party CAN | CAN ID | Status |
|--------|----------|----------------------|--------|--------|
| Motor RPM / Axle Speed | Yes | Yes | 0x108 | Decoded |
| Motor Torque (actual) | Yes | Yes | 0x108 | Decoded |
| Road Speed | Yes | Yes | 0x257, 0x155 | Decoded |
| Steering Angle | Yes | Yes | 0x370, 0x129 | Decoded |
| Throttle/Pedal Position | Yes | Yes | 0x118 | Decoded |
| Brake State | Yes | Yes | 0x145, 0x118 | Decoded |
| Brake Torque | Yes | Yes | 0x145 | Not yet implemented |
| Gear (P/R/N/D) | Yes | Yes | 0x118 | Decoded |
| Longitudinal Acceleration | Yes | **No** | N/A | Needs separate IMU |
| Lateral Acceleration | Yes | **No** | N/A | Needs separate IMU |
| Yaw Rate | Nice to have | **No** | N/A | Needs separate IMU |
| Battery SoC | Nice to have | Yes (Vehicle bus) | 0x292 | Not yet implemented |
| Battery Voltage | Nice to have | Yes (Vehicle bus) | 0x132 | Not yet implemented |
| Battery Temperature | Nice to have | Yes (Vehicle bus) | 0x312 | Not yet implemented |

### Coverage: 8 of 11 required signals available on Party CAN

The three missing signals (longitudinal accel, lateral accel, yaw rate) require a separate IMU module. Options:
- BNO055 (~$2) — I2C/SPI, 9-axis, built-in sensor fusion
- MPU6050 (~$1) — I2C, 6-axis, no sensor fusion
- Derive from speed differentials (less accurate, higher latency)

---

## CAN Bus Architecture

### Bus Topology

| Bus | Name | IDs | Physical Access |
|-----|------|-----|----------------|
| Bus 0 | Party CAN | 11-bit, 500kbps | OBD2 port (pins 6/14), X179 connector |
| Bus 1 | Vehicle CAN | 11-bit, 500kbps | X179 connector only |
| Bus 2 | Autopilot Party CAN | 11-bit, 500kbps | Requires separate connection |
| N/A | Chassis CAN | 11-bit, 500kbps | Internal only |
| N/A | Ethernet backbone | — | Internal only |

## Commercial Products — How They Connect

**Every Tesla performance/telemetry device uses the same approach**: a harness that plugs into the proprietary 26-pin diagnostic connector (behind rear center console / under seat) and provides a standard 16-pin OBD2 female socket. Then an OBD2 dongle (OBDLink, ELM327) plugs into that harness. The dongle operates in raw CAN monitor mode (ATMA), not OBD2 PID query mode.

### The Standard Hardware Path

```
Tesla 26-pin diagnostic connector (behind rear center console)
  → "Model 3 & Model Y OBD2 Diagnostic Harness Scanner Splitter 26Pin Adapter"
    → Standard 16-pin OBD2 female socket
      → OBDLink / ELM327 / ESP32 dongle (raw CAN monitor mode)
        → BLE to phone/app
```

This harness is widely available (~$15-30) and is what tesLAX, Scan My Tesla, S3XY Commander, and all other commercial products use. It provides:
- CAN-H / CAN-L on standard OBD2 pins 6/14
- 12V power on pin 16
- Ground on pin 4/5

**Our hardware**: We already have this 26-pin OBD2 harness for the Model Y ("Model 3 & Model Y OBD2 Diagnostic Harness Scanner Splitter 26Pin Adapter for TSL Model 3 & Model Y Post Jan 2019 to Now"). It provides a standard OBD2 port that allows reading of CAN data from the vehicle's diagnostic bus.

### Confirmed Commercial Products

| Product | Connection | Protocol | How It Works | Price |
|---------|-----------|----------|-------------|-------|
| **Scan My Tesla** (Android) | 26-pin harness → OBD2 → OBDLink LX/MX | Raw CAN (ELM327 ATMA) | Passively reads CAN frames via STN chip in monitor mode. Shows live power/torque/RPM while driving (Bjorn Nyland YouTube) | App ~$15 + OBDLink ~$100 |
| **tesLAX** (iOS) | 26-pin harness → OBD2 → OBDLink MX+ (BT Classic) or CX (BLE) | Raw CAN (ST commands) | Same passive CAN monitor. Has "Drag Timer" for measuring acceleration while driving | App ~$5-15 + OBDLink ~$100 |
| **T Sportline MSX Display** | Inline harness into diagnostic connector + 12V | Direct CAN | Physical LCD, taps CAN directly (no dongle) | ~$500 |
| **S3XY Commander/Dash** (Enhance Auto) | 26-pin harness pins 13/14 (Bus 6) | Raw CAN (MCP2515) | Commercial ESP32 + CAN product | ~$200+ |
| **Feifan Commander** (69K+ sales, China) | 26-pin harness pins 13/14 | Raw CAN | Same approach, dominant in Chinese market | ~$50-100 |
| **Hansshow Linux Display** | Inline harness into diagnostic connector | Direct CAN | Physical display | ~$200-400 |
| **flipper-tesla-fsd** (open source) | 26-pin harness → OBD2 or direct CAN | Raw CAN (MCP2515) | Open source, $6-14 in parts | Free + ~$14 hardware |
| **TM-Spy** | Same as Scan My Tesla | Raw CAN | Community app | App ~$10 |
| **CANserver (JWardell)** | X179 connector → ESP32 + SN65HVD230 | Raw CAN (ESP32 TWAI) | WiFi server, streams CAN data to web dashboard | ~$60 |

**Products not found**: "Thor", "JTLD", "Panthera" — may be regional/niche products with limited web presence. "Miltek" (Milltek Sport) makes exhaust sound generators for Teslas, not performance displays.
| **Scan My Tesla** (Android) | X179 → OBD2 adapter cable → OBDLink LX/MX | Raw CAN (ELM327 ATMA) | Passively reads CAN frames via STN chip in monitor mode |
| **tesLAX** (iOS) | X179 → OBD2 adapter cable → OBDLink MX+ (BT Classic) or CX (BLE) | Raw CAN (ST commands) | Same passive CAN monitor. Has "Drag Timer" for driving acceleration measurement |
| **T Sportline MSX Display** | Inline harness into diagnostic connector + 12V | Direct CAN | Physical LCD, taps CAN directly (no dongle) |
| **S3XY Commander/Dash** (Enhance Auto) | X179 pins 13/14 (Bus 6) | Raw CAN (MCP2515) | Commercial ESP32 + CAN product ($200+) |
| **Feifan Commander** (69K+ sales, China) | X179 pins 13/14 | Raw CAN | Same approach, dominant in Chinese market |
| **Hansshow Linux Display** | Inline harness into diagnostic connector | Direct CAN | Physical display |
| **flipper-tesla-fsd** (open source) | OBD-II port OR X179 connector | Raw CAN (MCP2515) | Open source, $6-14 in parts |
| **TM-Spy** | Same as Scan My Tesla | Raw CAN | Community app |

**Key insight**: Products that use OBDLink dongles do NOT use OBD2 PID queries. They use the ELM327/STN chip purely as a CAN-to-serial bridge in ATMA (monitor) mode, decoding signals with community DBC files.

**Could not find**: "Thor", "JTLD", "Panthera" — may be regional/niche products with limited web presence. "Miltek" (Milltek Sport) makes exhaust sound generators for Teslas, not performance displays.

### Does It Work While Driving?

**Yes, definitively confirmed by multiple products:**
- Scan My Tesla shows live power/torque/RPM while driving (documented in Bjorn Nyland's YouTube videos)
- tesLAX has a "Drag Timer" feature specifically for measuring acceleration while driving
- flipper-tesla-fsd explicitly works while driving on both OBD-II and X179 connectors
- S3XY Commander is a live driving display

**Caveat**: The OBD-II port access point "may go silent in Drive on some Model 3/Y builds" (flipper-tesla-fsd HARDWARE.md). The X179 connector stays active in all modes.

### Accelerometer/G-Force Data

NOT available on any Tesla CAN bus. Every device that shows g-force uses:
- **Phone IMU**: tesLAX and Scan My Tesla use the phone's built-in accelerometer
- **External IMU**: BNO055 or MPU6050 connected to the CAN interface
- **Derived from speed**: Differentiate vehicle speed (low accuracy)

### Protocol Details

| Property | Value | Source |
|----------|-------|--------|
| CAN Standard | CAN 2.0B (11-bit standard IDs) | All DBC IDs < 0x7FF |
| CAN-FD | Used on some buses | commaai/panda firmware |
| Bit rate | 500 kbps | MountainPass/Sasha Anis |
| ID format | 11-bit (standard frame) | All DBC files |
| Diagnostic protocol | UDS (ISO 14229) | commaai FW_QUERY_CONFIG |
| Encryption | None (plaintext) | Confirmed by multiple researchers |
| Integrity | Checksum per frame | Must compute for injection, not needed for reading |
| Counter | Rolling counter per message | ECUs reject stale/duplicate counters |

### Checksum Algorithm (from commaai/opendbc)

Reading frames does NOT require checksum computation. The checksum is for frame injection validation.

```python
def tesla_checksum(address: int, sig, d: bytearray) -> int:
    checksum = (address & 0xFF) + ((address >> 8) & 0xFF)
    checksum_byte = sig.start_bit // 8
    for i in range(len(d)):
        if i != checksum_byte:
            checksum += d[i]
    return checksum & 0xFF
```

---

## Physical Connection Points

### Primary Route: 26-pin Diagnostic Harness → OBD2 Adapter

This is the standard approach used by ALL commercial Tesla telemetry products. We already have the hardware.

**What we have**: "Model 3 & Model Y OBD2 Diagnostic Harness Scanner Splitter 26Pin Adapter for TSL Model 3 & Model Y Post Jan 2019 to Now"

**How it works**:
1. Unplug the 26-pin connector behind the rear center console (under seat area)
2. Plug in the splitter harness (pass-through — original connection preserved)
3. Harness provides a standard 16-pin OBD2 female socket
4. Plug OBD2 dongle (ELM327, OBDLink, etc.) into the socket
5. Dongle reads raw CAN frames in monitor mode (ATMA)
6. CAN data streams via BLE to phone/app

**Bus accessed**: Bus 6 (gateway-mixed forwarding from Party CAN + Vehicle CAN)
- Stays active in ALL drive modes
- Routes signals from multiple internal buses
- Provides motor RPM, torque, speed, steering, throttle, brake, gear, BMS data

**26-pin harness pinout (Post Jan 2019):**

| Pin | Signal |
|-----|--------|
| 13 | CAN-H (Bus 6) |
| 14 | CAN-L (Bus 6) |
| 15 | 12V |
| 26 | GND |

The harness maps these to standard OBD2 pins:
- CAN-H → OBD2 pin 6
- CAN-L → OBD2 pin 14
- 12V → OBD2 pin 16
- GND → OBD2 pins 4/5

### Alternative: Direct X179 Tap (ESP32 + CAN Transceiver)

For custom hardware (no OBD2 dongle), connect directly to the 26-pin connector:
- CAN-H on pin 13, CAN-L on pin 14
- 12V on pin 15, GND on pin 26
- MCP2515 or SN65HVD230 CAN transceiver → ESP32 TWAI → BLE
- No second termination resistor needed (Tesla already terminates)
- Cost: ~$20 in parts (ESP32 + CAN transceiver + pigtail)

### ESP32 + SN65HVD230 CAN-to-BLE Adapter (Our Build)

**Components we have:**
- ESP32 dev board
- SN65HVD230 VP230 CAN transceiver board
- OBD2 male plug with pre-wired leads (ordered)

**Wiring:**

| OBD2 Plug Pin | Wire Color | Signal | Connect To |
|---------------|-----------|--------|------------|
| 4 | Orange | Chassis GND | ESP32 GND + SN65HVD230 GND |
| 5 | Yellow | Signal GND | (optional, tie to pin 4) |
| 6 | Green | CAN-H | SN65HVD230 CANH |
| 14 | Brown/White | CAN-L | SN65HVD230 CANL |
| 16 | Green/White | +12V | Buck converter → ESP32 VIN |

Cut remaining 11 wires short — not needed.

**ESP32 ↔ SN65HVD230 wiring:**

| ESP32 Pin | SN65HVD230 Pin | Notes |
|-----------|---------------|-------|
| GPIO 21 (TX) | TXD | ESP32 TWAI transmit |
| GPIO 22 (RX) | RXD | ESP32 TWAI receive |
| 3.3V | VCC | |
| GND | GND | |

Note: The ESP32 has a built-in CAN controller (TWAI — Two-Wire Automotive Interface). No MCP2515 needed — that's only for devices without a built-in CAN controller (e.g. Arduino Uno). The SN65HVD230 handles only the physical layer (3.3V logic ↔ CAN differential signaling).

### Power Behavior on Model 3/Y

**The 12V on the diagnostic connector is only live when the vehicle is awake.** ([TeslaTap](https://teslatap.com/modifications/extracting-internal-vehicle-data/))

- Power is ON when the car is "awake" (ignition on, door open, app active)
- Power is OFF when the car sleeps (typically 15-30 min after last activity)
- The CAN bus also goes quiet when the car sleeps
- No vampire drain risk — Tesla cuts power automatically
- The device will power down and restart each time the car wakes — fine for driving sessions

This differs from Model S/X pre-2021 where the diagnostic port was always-on.

**Implication**: Safe to power ESP32 from OBD2 pin 16. No deep sleep firmware needed. No battery drain concern. Device is active while driving, off while parked.

**ESP32 power draw**: ~150-200mA at 12V (~3-4 Ah/day). Negligible during a drive. Car cuts power before it matters.

### NOT Recommended: Under-Dash OBD2 Port

- The white connector under the steering column is **locked/encrypted**
- It does NOT carry standard CAN data
- Some early Model 3 builds had no connector at all
- Highland/Juniper (2024+) switched to DoIP (Ethernet) here

**X179 Pinout (20-pin connector):**

| Pin | Signal |
|-----|--------|
| 1 | 12V |
| 13 | CAN-H (Bus 6) |
| 14 | CAN-L (Bus 6) |
| 15 | 12V |
| 20 | GND |

**X179 Pinout (26-pin Highland/Junior):**

| Pin | Signal |
|-----|--------|
| 13 | CAN-H |
| 14 | CAN-L |
| 15 | 12V |
| 26 | GND |

### 3. Front Diagnostic Connector (White 5-pin, Sumitomo)

- **Location**: Under driver dash, above dead pedal
- **Status**: **Locked/encrypted** — NOT standard CAN
- **Do not use**

---

## CAN Signal Definitions (Party CAN, Bus 0)

All signals below are on Party CAN (bus 0) unless noted. Sourced from:
- `opendbc/dbc/tesla_model3_party.dbc` (commaai/opendbc)
- `joshwardell/model3dbc` (community DBC, all three buses)
- `opendbc/car/tesla/carstate.py` (openpilot signal usage)

### Motor RPM & Torque — CAN ID 0x108 (264)

Message: `DI_torque`

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| DI_axleSpeed | 40 | 16 | Motorola (1-) | 0.1 | 0 | -2750..2750 | RPM |
| DI_torqueActual | 27 | 13 | Motorola (1-) | 2 | 0 | -7500..7500 | Nm |
| DI_torqueCommand | 12 | 13 | Motorola (1-) | 2 | 0 | -7500..7500 | Nm |

**openpilot usage**: Uses `DI_torqueActual` for motor torque feedback.

### Road Speed — CAN ID 0x257 (599)

Message: `DI_speed`

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| DI_vehicleSpeed | 12 | 12 | Motorola (1+) | 0.08 | -40 | -40..285 | kph |
| DI_uiSpeed | 24 | 8 | Motorola (1+) | 1 | 0 | 0..254 | raw |

Alternative speed source on CAN ID 0x155 (341), message `ESP_B`:
| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| ESP_vehicleSpeed | 42 | 10 | Motorola (1+) | 0.5 | 0 | 0..511 | kph |

### Steering Angle — CAN ID 0x370 (880) and 0x129 (297)

**Primary source** (used by openpilot): CAN ID 0x370, message `EPAS3S_sysStatus`

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| EPAS3S_internalSAS | 37 | 14 | Intel (0+) | 0.1 | -819.2 | -819.2..819 | deg |
| EPAS3S_torsionBarTorque | 19 | 12 | — | 0.01 | -20.5 | — | Nm |
| EPAS3S_handsOnLevel | 39 | 2 | — | 1 | 0 | 0..3 | level |
| EPAS3S_eacStatus | — | — | — | — | — | — | status |
| EPAS3S_steeringFault | — | — | — | — | — | — | flag |

**Secondary source**: CAN ID 0x129, message `SCCM_steeringAngleSensor`

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| SCCM_steeringAngle | 16 | 14 | Motorola (1+) | 0.1 | -819.2 | -819.2..819 | deg |
| SCCM_steeringAngleSpeed | 32 | 14 | Motorola (1+) | 0.5 | -4096 | -4096..4095.5 | deg/s |

**openpilot usage**: `steering_angle = -epas_status["EPAS3S_internalSAS"]` (negated)

### Throttle — CAN ID 0x118 (280)

Message: `DI_systemStatus`

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| DI_accelPedalPos | 32 | 8 | Motorola (1+) | 0.4 | 0 | 0..100 | % |
| DI_gear | 21 | 3 | — | 1 | 0 | — | enum (P/R/N/D) |
| DI_brakePedalState | 19 | 2 | — | 1 | 0 | 0..2 | enum (OFF/ON/INVALID) |
| DI_systemState | 16 | 3 | — | 1 | 0 | — | enum (5=ENABLE) |

### Brake — CAN ID 0x145 (325) and 0x39D (925)

**CAN ID 0x145**, message `ESP_status`:

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| ESP_driverBrakeApply | 29 | 2 | — | 1 | 0 | 0..2 | enum (2=applying) |
| ESP_brakeApply | 31 | 1 | — | 1 | 0 | 0..1 | bool |
| ESP_brakeTorqueTarget | 51 | 13 | Motorola (1+) | 2 | 0 | 0..16382 | Nm |

**CAN ID 0x39D**, message `IBST_status`:

| Signal | Start Bit | Length | Byte Order | Scale | Offset | Range | Unit |
|--------|-----------|--------|------------|-------|--------|-------|------|
| IBST_driverBrakeApply | 16 | 2 | — | 1 | 0 | 0..2 | enum |
| IBST_sInputRodDriver | — | — | — | — | — | — | mm |

**openpilot brake detection**: `cp_party.vl["ESP_status"]["ESP_driverBrakeApply"] == 2`

### Additional Signals

| CAN ID | Message | Signal | Description |
|--------|---------|--------|-------------|
| 0x155 (341) | ESP_B | ESP_vehicleStandstillSts | Standstill flag |
| 0x155 (341) | ESP_B | ESP_wheelPulseCount | 4 wheels |
| 0x249 (585) | SCCM_leftStalk | SCCM_turnIndicatorStalkStatus | Turn signals |
| 0x257 (599) | DI_speed | DI_uiSpeedUnits | Speed units |
| 0x399 (921) | DAS_settings | DAS_driverAccelerationMode | Chill/Standard |
| 0x39B (923) | DAS_status | DAS_autopilotState | Autopilot state |
| 0x39B (923) | DAS_status | DAS_forwardCollisionWarning | FCW |
| 0x39B (923) | DAS_status | DAS_blindSpotRearLeft/Right | Blind spot |

### BMS Signals (Vehicle CAN, Bus 1 — may need X179)

| CAN ID | Message | Signal | Description |
|--------|---------|--------|-------------|
| 0x132 | BMS_hvBusStatus | Pack voltage, pack current | HV battery electrical |
| 0x292 | BMS_socStatus | State of Charge (%) | Battery level |
| 0x312 | BMS_thermalStatus | Battery temperature | Thermal management |
| 0x33A | UI_ratedConsumption | Wh/km | Energy efficiency |

---

## Accelerometer / IMU Data

**NOT available on any Tesla CAN bus.** The ESP_status message (0x145) only contains quality flags (`ESP_lateralAccelQF`, `ESP_longitudinalAccelQF`), not actual acceleration values.

The actual IMU data is either:
- Internal to the autopilot computer (not broadcast)
- On the Autopilot Party CAN (bus 2), which requires a separate physical connection

**openpilot** does not read accelerometer data from the Tesla CAN bus — it uses its own built-in IMU on the comma device.

### Solutions for Accelerometer Data

| Approach | Cost | Accuracy | Latency | Notes |
|----------|------|----------|---------|-------|
| BNO055 IMU module | ~$2 | High | <5ms | 9-axis, built-in fusion |
| MPU6050 IMU module | ~$1 | Medium | <5ms | 6-axis, no fusion |
| Derive from speed | $0 | Low | 50-100ms | Differentiate DI_vehicleSpeed |
| CAN bus + IMU combo | ~$25 | High | <5ms | ESP32 + CAN + BNO055 |

---

## ELM327 Feasibility Analysis

### Can the ELM327 Read Tesla CAN Data?

**Theoretically possible, practically problematic.**

The ELM327 can be put in CAN monitor mode (ATMA) to passively listen to CAN frames. If connected to the Tesla OBD2 port, it would see Party CAN traffic.

### Throughput Bottleneck

| Parameter | Value |
|-----------|-------|
| CAN bus speed | 500 kbps |
| Max CAN frames/sec at 500kbps | ~4,000-5,000 |
| ELM327 serial baud (typical) | 115,200 bps |
| Max text output at 115,200 | ~460 frames/sec |
| **Capture rate** | **~10% of bus traffic** |

At 115200 baud serial, the ELM327 can relay approximately 460 frames/second as text. The Party CAN bus at 500kbps with 10-20 active message IDs at 10-100ms intervals generates far more traffic than the ELM327 can relay.

### Additional Issues
- BLE transport adds latency on top of serial bottleneck
- ELM327 internal buffer (64-256 bytes) causes frame drops when serial can't keep up
- ELM327 command protocol is designed for OBD2 polling, not raw CAN streaming
- Clone chips (STN2120, PIC-based) are unreliable at sustained 500kbps

### Verdict

**ELM327 ATMA is NOT recommended for Tesla telemetry.** Dropped frames would result in inconsistent telemetry data. A dedicated CAN interface is required.

---

## Alternative: Tesla Fleet API

Tesla provides a cloud-based Fleet API for vehicle data.

| Aspect | CAN Bus | Fleet API |
|--------|---------|-----------|
| Update rate | 10-100 Hz | ~1 Hz |
| Motor torque | Yes | No |
| Steering angle | Yes | No |
| Per-wheel speed | Yes | No |
| Speed | Yes | Yes (low rate) |
| Battery SoC | Yes | Yes |
| GPS | N/A | Yes |
| Lock/unlock | N/A | Yes |
| Internet required | No | Yes |
| Cost | Hardware ($20-1000) | Free (credentials) |

**Repository**: [timdorr/tesla-api](https://github.com/timdorr/tesla-api) (2.1k stars)

---

## Prior Art — GitHub Projects

### commaai/opendbc (3.1k stars)
- **URL**: https://github.com/commaai/opendbc
- **DBC file**: `opendbc/dbc/tesla_model3_party.dbc` (488 lines)
- **Car port**: `opendbc/car/tesla/` — full carstate.py, carcontroller.py, teslacan.py, values.py
- **Signals decoded**: Full Party CAN (motor RPM, torque, speed, steering, throttle, brake, gear, autopilot state)
- **Checksum algorithm**: Implemented in teslacan.py

### joshwardell/model3dbc (382 stars)
- **URL**: https://github.com/joshwardell/model3dbc
- **DBC file**: Covers all three buses (ChassisBus, VehicleBus, PartyBus)
- **Used by**: tesLAX, SavvyCAN, CANserver
- **Signals**: BMS, HVAC, door status, UI state, GPS — more comprehensive than opendbc party DBC

### hypery11/flipper-tesla-fsd (722 stars)
- **URL**: https://github.com/hypery11/flipper-tesla-fsd
- **Hardware**: Flipper Zero + CAN add-on OR ESP32 + MCP2515 ($14)
- **Connection**: X179 connector or OBD2 port
- **CAN handlers**: 37 total (14 TX, 23 RX)
- **Key CAN IDs**: 0x132 (BMS), 0x292 (SoC), 0x312 (thermal), 0x370 (EPAS), 0x39B (DAS), 0x3FD (FSD)
- **Confirmed working while driving**

### joshwardell/CANserver (112 stars)
- **URL**: https://github.com/joshwardell/CANserver
- **Hardware**: ESP32 + SN65HVD230 CAN transceiver (~$60 assembled)
- **Output**: WiFi web dashboard with battery voltage, current, power, temperatures, SoC
- **Purpose-built for Tesla Model 3**

### tesLAX (iOS App)
- **Hardware**: OBDLink MX+ (BT Classic) or OBDLink CX (BLE) + Tesla cable
- **Features**: Full CAN visualization, logging, DBC import
- **Cost**: ~$100-200 hardware + $5-15 app
- **Note**: Investigating whether this proves OBD2 port works while driving

### Adminius/ESP32-ScanMyTesla (64 stars)
- **URL**: https://github.com/Adminius/ESP32-ScanMyTesla
- **Hardware**: ESP32 + SN65HVD230 (not ELM327)
- **Note**: Does NOT work with iOS (no Bluetooth Serial support)

### bassmaster187/TeslaLogger (618 stars)
- **URL**: https://github.com/bassmaster187/TeslaLogger
- **Method**: Tesla Fleet API (cloud, not CAN)
- **Features**: Self-hosted data logging, charging stats, trip history

---

## Plan: DBC-Based Signal Decoding for vehicle-sim

### Strategy

The plan is to use **DBC files** as the signal definition source, matching the approach used by every commercial Tesla product (tesLAX imports DBC files, Scan My Tesla uses them, openpilot is built on them). This is a data-driven approach — the DBC file defines all CAN IDs, signal names, bit positions, scaling, and offsets. Our `DBCSignalTranslator` reads these definitions and extracts signal values from raw CAN frames.

### DBC Sources

| DBC File | Source | Coverage | Best For |
|----------|--------|----------|----------|
| `tesla_model3_party.dbc` | [commaai/opendbc](https://github.com/commaai/opendbc) | Party bus only, 488 lines | Motor, speed, steering, brake, throttle, gear |
| `model3dbc` | [joshwardell/model3dbc](https://github.com/joshwardell/model3dbc) | All three buses | BMS, HVAC, doors, UI, GPS — more comprehensive |

### Current State
- `DBCSignalTranslator` already decodes CAN IDs 0x108 (DI_torque) and 0x129 (SCCM_steeringAngleSensor)
- `initializeCANMonitor()` puts ELM327 in ATMA mode for raw CAN frame streaming
- `parseCANFrame()` in ELM327Transport extracts CAN ID + 8 data bytes from ELM327 text output
- `DBCTranslationService` already loads DBC-based vehicle configs

### Implementation Plan

1. **Expand DBC signal definitions** from `tesla_model3_party.dbc`:
   - 0x257 (DI_speed) — road speed
   - 0x370 (EPAS3S_sysStatus) — steering angle (primary source, per openpilot)
   - 0x145 (ESP_status) — brake state and torque
   - 0x155 (ESP_B) — standstill, vehicle speed (secondary)
   - 0x118 (DI_systemStatus) — throttle pedal, gear, brake pedal state

2. **Signal parsing**: Use DBC-defined bit extraction (start bit, length, byte order, scale, offset). The existing `DBCTranslationService` architecture supports this.

3. **Hardware path**: Our 26-pin OBD2 harness + ELM327 in ATMA mode for initial testing. If throughput is insufficient (likely), upgrade to ESP32 + CAN transceiver or OBDLink CX.

4. **No IMU needed**: Motor torque (0x108 DI_torqueActual) provides direct driving force data. Acceleration can be derived from speed differentials if needed.

### Data Flow

```
Tesla 26-pin diagnostic connector (behind rear center console)
  → OBD2 harness splitter (our hardware)
    → OBD2 dongle (ELM327 ATMA or ESP32 + CAN)
      → Raw CAN frames via BLE notification
        → vehicle-sim BLEManager (invokeDataCallback)
          → DBCSignalTranslator (DBC signal extraction per tesla_model3_party.dbc)
            → VehicleSim (telemetry state model)
```

### Risk: ELM327 Throughput

The ELM327 may not keep up with Tesla's 500kbps CAN bus (see Throughput section above). If frame drops make telemetry unreliable:
- **Option A**: OBDLink CX (~$100) — better BLE throughput, used by tesLAX
- **Option B**: ESP32 + MCP2515 (~$20) — dedicated CAN controller, no serial bottleneck
- **Option C**: CANserver (~$60) — pre-built ESP32 CAN-to-WiFi solution

---

## Sources

- [commaai/opendbc](https://github.com/commaai/opendbc) — Tesla DBC files and car port
- [commaai/opendbc tesla_model3_party.dbc](https://github.com/commaai/opendbc/blob/master/opendbc/dbc/tesla_model3_party.dbc)
- [commaai/opendbc car/tesla/carstate.py](https://github.com/commaai/opendbc/blob/master/opendbc/car/tesla/carstate.py)
- [commaai/opendbc car/tesla/values.py](https://github.com/commaai/opendbc/blob/master/opendbc/car/tesla/values.py)
- [commaai/panda](https://github.com/commaai/panda) — CAN/CAN-FD hardware
- [joshwardell/model3dbc](https://github.com/joshwardell/model3dbc) — Community Tesla DBC (all buses)
- [joshwardell/CANserver](https://github.com/joshwardell/CANserver) — ESP32 CAN-to-WiFi
- [hypery11/flipper-tesla-fsd](https://github.com/hypery11/flipper-tesla-fsd) — Tesla CAN toolkit
- [TMC Diagnostic Port Index](https://teslamotorsclub.com/tmc/threads/diagnostic-port-index.98663/)
- [tesLAX](https://teslax.app) — iOS CAN bus visualization
- [timdorr/tesla-api](https://github.com/timdorr/tesla-api) — Fleet API documentation
- [TeslaTap — Extracting Internal Vehicle Data](https://teslatap.com/modifications/extracting-internal-vehicle-data/) — OBD2 port power behavior (Model 3/Y: power only when vehicle awake)
- [TMC — OBD-II Port Power Failure](https://teslamotorsclub.com/tmc/threads/help-obd-ii-port-power-failure.89037/) — Diagnostic port fuse and power issues
- [OBDLink — Tesla Model 3 Cable Guide](https://obdlink.nl/en/frequently-asked-questions/manuals/tesla-model-3-cable) — Installation procedure
- [Tesla Owners Online — Diagnostic Port and Data Access](https://www.teslaownersonline.com/threads/diagnostic-port-and-data-access.7502/) — Community reverse-engineering thread
- [EV Clinic — Model 3 12V Battery Failure](https://evclinic.eu/2025/04/12/model-3-12v-battery-failure-mistakes-owners-make/) — 12V system behavior
