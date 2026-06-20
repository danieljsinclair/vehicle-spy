# Audi e-tron (2021, MLB Evo) -- ESP32 Direct CAN Notes

Companion to `audi-etron-obd2-research.md` (which covers why ELM327 clone adapters fail).
This document covers the **ESP32 + SN65HVD230 direct CAN approach**: hardware compatibility,
firmware config, DBC coverage, and what needs changing in vehicle-sim.

---

## Wiring Diagram

```
  OBD2 PORT                     SN65HVD230                    ESP32-WROOM-32
┌───────────┐                 ┌──────────────┐              ┌──────────────┐
│ 4   GND ──┼─────────────────┤ GND        GND├──────────────┤ GND          │
│ 5   GND ──┼──── ─ ┐    ┌───►│ GND         3V3├──────────────┤ 3V3          │
│           │      │    │     │              TX├──────────────┤ D22          │
│ 6   CANH ─┼──── ──┼────┼───►│ CANH        RX├──────────────┤ D21          │
│ 14  CANL ─┼──── ──┼────┼───►│ CANL          │              │              │
│           │      │    │     │              │              │              │
└───────────┘      │    │     └──────────────┘              │              │
                   │    │                                    │              │
         (pins 4+5 share GND with transceiver and ESP32)     │              │
                                                             └──────────────┘
```

### Wire colors

| Wire    | Color  | From                     | To                      |
|---------|--------|--------------------------|-------------------------|
| 3.3V    | Red    | ESP32 3V3                | SN65HVD230 VCC / 3.3V   |
| GND     | Brown  | OBD2 pins 4+5, transceiver GND, ESP32 GND (all tied together) |
| TX      | Yellow | SN65HVD230 TX            | ESP32 D22 (GPIO 22)     |
| RX      | Orange | SN65HVD230 RX            | ESP32 D21 (GPIO 21)     |
| CAN-H   | Blue   | OBD2 pin 6               | SN65HVD230 CANH         |
| CAN-L   | Green  | OBD2 pin 14              | SN65HVD230 CANL         |

---

## Hardware Compatibility: ESP32 + SN65HVD230

**Verdict: Hardware is compatible, but signal availability is uncertain.**

| Parameter | ESP32 + SN65HVD230 | Audi e-tron OBD2 CAN |
|-----------|-------------------|----------------------|
| Voltage | 3.3V transceiver | 3.3V CAN transceiver levels |
| Speed | Up to 1 Mbps | 500 kbps (ISO 15765-4) |
| Pins | OBD2 pin 6 (CAN-H), pin 14 (CAN-L) | Standard SAE J2284 |
| Mode | LISTEN_ONLY (no transmit) | Passive sniffing only |

The SN65HVD230 is a 3.3V CAN transceiver rated up to 1 Mbps. The e-tron diagnostics CAN
bus runs at 500 kbps. The firmware (`firmware/can-bridge/can-bridge.ino`) is already
configured correctly:

- TWAI driver in `TWAI_MODE_LISTEN_ONLY` -- safe, never transmits on vehicle bus
- `TWAI_TIMING_CONFIG_500KBITS()` -- matches Audi diagnostics bus speed
- GPIO 22 (TX) / GPIO 21 (RX) -- standard ESP32 TWAI pins

No hardware or firmware changes needed for basic CAN frame capture.

---

## CAN Bus Architecture: The Gateway Problem

The e-tron has multiple internal CAN buses connected through the J533 CAN Gateway:

| Internal Bus | Content | OBD2 Exposure |
|-------------|---------|---------------|
| Powertrain CAN | Motor controllers, battery, charger | Gateway-routed (filtered) |
| Chassis CAN | ESP, steering, brakes, wheel speeds | Gateway-routed (filtered) |
| Comfort CAN | Doors, windows, HVAC, lighting | Partially gateway-routed |
| Infotainment CAN | MIB display, navigation | Blocked |
| ADAS/Extended CAN | ACC radar, camera, sensors | Blocked |

**The critical unknown**: What CAN messages does the J533 gateway actually forward to the
OBD2 diagnostics bus?

### What we know (evidence-based)

1. **opendbc carstate.py** reads MLB signals from THREE buses: `pt` (powertrain), `cam` (camera),
   `alt` (alternate). The `alt` bus carries `Getriebe_03` (gear) and `TSK_04` (cruise).
   The comma.ai hardware uses a dedicated J533 harness that taps the gateway directly.

2. **Ross-Tech Wiki** confirms MLB vehicles use Address 19 (CAN Gateway, J533) with gateway
   modules "GW-BEM 4CAN-M", "5CAN-M", "6CAN" variants. The number indicates internal bus count.

3. **Real-world test (2026-05-07)** showed zero CAN frames on the OBD2 port with the vehicle
   parked. This was with an ELM327 clone in ATMA mode -- the bus was silent.

### What we do NOT know (requires physical testing)

- Whether the OBD2 diagnostics bus carries any of the telemetry signals at all
- Which messages from `vw_mlb.dbc` are actually visible on OBD2 pins 6/14
- Whether ignition ON / drive-ready state causes the gateway to forward powertrain messages
- Whether a 2021 e-tron has any gateway firmware restrictions (TSB 2078077/2 mentions
  "gateway communication and flashing restrictions" for 2023-2024 MLB Evo vehicles)

### Likely scenario

Based on the architecture, the OBD2 diagnostics bus most likely carries:
- **Probably visible**: ESP_01 (vehicle speed), ESP_02 (acceleration), ESP_05 (brake pressure)
  -- these are gateway-routed and used by multiple ECUs
- **Possibly visible**: Motor_03 (RPM, throttle), Getriebe_03 (gear) -- powertrain data
  that the gateway may or may not forward to diagnostics bus
- **Unlikely visible**: ADAS, infotainment, detailed EV-specific signals (battery SoC, motor temps)

The comma.ai approach uses a physical J533 harness that plugs into the gateway module itself
(pulling the gateway connector and inserting a tap). This bypasses OBD2 entirely. Passive
OBD2 listening may only see a subset of what their hardware captures.

---

## DBC File Coverage: vw_mlb.dbc

The project uses `resources/dbc/vw_mlb.dbc` from commaai/opendbc. Stats:
- **145 messages** (BO_ entries)
- **1442 signals** (SG_ entries)
- **16 ECU nodes** listed

### Currently mapped signals (DefaultVehicleConfigs.cpp)

Only 3 signals mapped for `audi_mlb_evo`:

| DBC Signal | VehicleSignal Field | CAN ID | Message | Source ECU |
|-----------|-------------------|--------|---------|------------|
| `ESP_v_Signal` | `speedKmh` | 0x100 | ESP_01 | Gateway_D4C7 |
| `ESP_Laengsbeschl` | `accelerationG` | 0x101 | ESP_02 | Gateway_D4C7 |
| `ESP_Bremsdruck` | `brakePercent` | 0x106 | ESP_05 | Gateway_D4C7 |

Note: `ESP_Bremsdruck` maps to `brakePercent` but is actually brake pressure in Bar
(range -30 to 276.6 Bar). This is a semantic mismatch -- the field name implies percentage
but the DBC signal is pressure.

### Additional signals available in vw_mlb.dbc

These signals exist in the DBC file and could be mapped. Availability on the OBD2 bus
is unconfirmed (see gateway problem above).

| DBC Signal | Description | CAN ID | Message | Could Map To |
|-----------|-------------|--------|---------|-------------|
| `MO_Drehzahl_01` | Engine RPM (0.25 rpm/bit) | 0x105 | Motor_03 | `motorRpm` |
| `MO_Fahrpedalrohwert_01` | Throttle position (0.4%/bit) | 0x105 | Motor_03 | `throttlePercent` |
| `MO_Mom_Fahrerwunsch` | Driver torque request | 0x080 | Motor_01 | `motorTorqueNm` |
| `GE_Waehlhebel` | Gear selector position | 0x102 | Getriebe_03 | `gearSelector` |
| `GE_Zielgang` | Target gear | 0x102 | Getriebe_03 | `gearRequested` |
| `ESP_VL_Radgeschw` | Front-left wheel speed | 0x103 | ESP_03 | (new: wheel speeds) |
| `LWI_01` | Steering wheel angle | varies | LWI_01 | (new: steering angle) |

**Gap vs Tesla config**: The Tesla Model 3 config maps 7 signals. The Audi MLB Evo config
maps only 3. Missing: motorRpm, throttlePercent, motorTorqueNm, gearSelector, gearRequested.

### DBC scope limitation

The `vw_mlb.dbc` file covers MLB-platform vehicles with combustion and hybrid powertrains.
The ECU nodes listed (Motor_EDC17_D4, Motor_ME17_BY, Motor_MED17_SIMOS8_D4) are ICE/hybrid
engine controllers. The 2021 e-tron is a pure BEV on the MLB Evo platform -- its motor
controllers and HV battery management signals may differ from what's defined in this DBC.

For e-tron-specific EV signals, the `vw_meb.dbc` file (referenced in
`audi-etron-obd2-research.md`) covers the MEB platform and includes BEV-specific ECUs
like `BMC_MLBevo` (Battery Management Controller), `DCDC_HV` (HV DC-DC converter), and
`Ladegeraet_2` (charger). However, `vw_meb.dbc` uses 48-byte extended frames (CAN FD) which
the current ESP32 TWAI driver and ELM327 ASCII protocol cannot handle.

---

## Gotchas and Limitations

### 1. Gateway filtering is the primary risk

The ESP32 + SN65HVD230 can physically capture CAN frames at 500 kbps. The question is
whether the J533 gateway forwards any useful telemetry to the diagnostics bus. Testing
with the vehicle in "drive ready" mode (ignition ON, foot on brake, drive selected) is
required. The parked/locked bus was completely silent.

### 2. ESP_Bremsdruck is pressure, not percentage

The current mapping `ESP_Bremsdruck` -> `brakePercent` is semantically wrong.
`ESP_Bremsdruck` is brake pressure in Bar (range -30 to 276.6). This either needs:
- A new VehicleSignal field like `brakePressureBar`, or
- A conversion factor in the signal mapping, or
- Renaming the field to clarify it is not a percentage

### 3. Limited EV-specific signals in vw_mlb.dbc

The DBC file is oriented toward ICE/hybrid MLB vehicles. Pure BEV signals (battery SoC,
motor temperature, HV voltage, charging status) are not defined. These may exist on
internal CAN buses not captured in this DBC, or may require `vw_meb.dbc` (which uses
CAN FD, incompatible with current firmware).

### 4. Single CAN bus speed

The firmware hardcodes 500 kbps. Some MLB Evo vehicles may use different speeds on
different internal buses. The diagnostics bus should be 500 kbps, but this should be
confirmed with the actual vehicle.

### 5. No multi-bus support

The current architecture reads a single CAN bus. The e-tron has 4-6 internal buses.
Reading multiple buses would require multiple transceivers or a gateway tap (comma.ai approach).

### 6. Frame format: standard 11-bit IDs only

The `vw_mlb.dbc` messages use 11-bit CAN IDs (0x040 to 0x522 range). The firmware's
`streamFrame()` formats as `%03X` (3 hex digits), which handles 11-bit IDs correctly.
No changes needed for standard CAN 2.0A frames.

---

## Recommended Code Changes

### Priority 1: Add more signal mappings (after confirming bus activity)

File: `src/domain/DefaultVehicleConfigs.cpp`

```cpp
VehicleConfig DefaultVehicleConfigs::audiMLBEvo() {
    return VehicleConfig(
        "resources/dbc/vw_mlb.dbc",
        "vw_mlb.dbc",
        "Audi MLB Evo",
        std::unordered_map<std::string, std::string>{
            {"ESP_v_Signal", "speedKmh"},
            {"ESP_Laengsbeschl", "accelerationG"},
            {"ESP_Bremsdruck", "brakePressureBar"},  // FIX: was brakePercent
            {"MO_Drehzahl_01", "motorRpm"},
            {"MO_Fahrpedalrohwert_01", "throttlePercent"},
            {"MO_Mom_Fahrerwunsch", "motorTorqueNm"},
            {"GE_Waehlhebel", "gearSelector"},
            {"GE_Zielgang", "gearRequested"}
        },
        "",   // canBus
        true   // isCANProtocol
    );
}
```

This brings Audi to parity with Tesla (7+ signals). But these mappings should only be
added after confirming the signals are visible on the OBD2 diagnostics bus.

### Priority 2: Fix brake signal semantics

Either rename `brakePercent` to `brakePressureBar` for Audi, or add a unit-aware field
to VehicleSignal so that the display layer can show the correct unit. The DBC signal
uses Bar, not percent.

### Priority 3: Investigate BEV-specific DBC

If e-tron EV signals are needed, evaluate whether `vw_meb.dbc` CAN FD frames can be
captured. This would require:
- ESP32 TWAI driver reconfigured for CAN FD (if supported, or use ESP32-S3 with TWAI FD)
- New frame parsing for 48-byte extended frames
- New DBC parser for vw_meb.dbc format

This is a significant change and probably not justified until basic CAN capture is proven
to work on the e-tron.

---

## Testing Plan

### Step 1: Confirm bus activity (ignition ON, drive ready)

```
vehicle-sim --wifi <esp32-ip> --vehicle audi_mlb_evo
```

Expected outcome if gateway forwards data: CAN frames appear with IDs matching
ESP_01 (0x100), ESP_02 (0x101), Motor_03 (0x105).

If zero frames: the gateway is not forwarding telemetry to diagnostics bus, and a
direct gateway tap (comma.ai style) would be needed.

### Step 2: Capture raw frames, compare with DBC

Log all CAN frames and compare IDs against `vw_mlb.dbc` BO_ entries. This reveals
exactly which messages the e-tron exposes on the OBD2 port.

### Step 3: Map visible signals

Based on Step 2 results, update signal mappings in `DefaultVehicleConfigs.cpp`.

---

## Sources

- `firmware/can-bridge/can-bridge.ino` -- ESP32 CAN bridge firmware (TWAI, 500kbps, listen-only)
- `resources/dbc/vw_mlb.dbc` -- 145 messages, 1442 signals, 16 ECUs
- `docs/audi-etron-obd2-research.md` -- ELM327 adapter failure analysis
- `docs/plans/esp32-can-integration.md` -- WiFi transport architecture
- commaai/opendbc `carstate.py` -- MLB vehicle uses pt/cam/alt buses
- commaai/opendbc `values.py` -- `VolkswagenMLBPlatformConfig` uses `vw_mlb` DBC
- Ross-Tech Wiki -- J533 CAN Gateway, Address 19, 500kbps diagnostics
