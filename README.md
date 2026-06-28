# vehicle-sim

Real-time vehicle CAN bus telemetry for EV sound simulation.

Reads CAN frames from a Tesla (or other vehicle) via an ESP32 + CAN transceiver, translates them through DBC signal definitions, and streams telemetry for display or physics simulation.

## Quick Start

### Prerequisites

- macOS with Xcode Command Line Tools (`xcode-select --install`)
- Homebrew

### Clone & Build

```bash
git clone <repo-url> && cd vehicle-sim
make install-deps     # First time only: installs cmake, imagemagick, arduino-cli
make                  # Builds native C++, runs tests, builds ESP32 firmware, builds iOS
```

### Demo (No Hardware)

```bash
./build-native/vehicle-sim --connect demo --vehicle tesla
```

## ESP32 CAN Bridge

An ESP32-WROOM-32 with an SN65HVD230 CAN transceiver reads the vehicle CAN bus and streams frames over WiFi TCP using a minimal ELM327 protocol. vehicle-sim connects to it the same way it connects to a BLE OBD2 adapter.

### Hardware

| Component | Purpose | ~Cost |
|-----------|---------|-------|
| ESP32-WROOM-32 dev board | WiFi + TWAI CAN controller | $5 |
| SN65HVD230 (VP230) CAN transceiver | CAN bus level shifting | $2 |
| OBD2 breakout cable | Connects to vehicle (pin 6 = CAN-H, pin 14 = CAN-L) | $5 |

**Note:** Many cheap ESP32 dev boards with USB-C have data pins not connected. If your board isn't detected by macOS, try a USB-A to USB-C adapter — the USB-C to USB-C handshake on these boards is often broken.

### Wiring

```
SN65HVD230 board    ESP32 DevKit     Dupont wire
────────────────    ────────────     ───────────
3.3V        ──────  3V3        ────  red
GND         ──────  GND        ────  orange   (tie to OBD pin 4 + pin 5)
TX          ──────  D22        ────  brown
RX          ──────  D21        ────  blue

SN65HVD230 CANH  ────  OBD2 pin 6   (green Dupont → green OBD2 wire)
SN65HVD230 CANL  ────  OBD2 pin 14  (black Dupont → brown/white OBD2 wire)
```

Harness wire colours are fixed by the OBD2 plug: GND = orange (pin 4) + yellow (pin 5), CAN-H = green (pin 6), CAN-L = brown/white (pin 14). The CAN-L Dupont wire is black. **Pin 13 trap:** pin 13 has a black/white wire that is vendor-specific, NOT CAN-L — it looks similar to pin 14's brown/white, and wiring CAN-L to it gives power-but-no-data. Tie both OBD GND pins (4 and 5) to common GND; don't leave pin 5 floating.

If you get no CAN data, swap TX and RX — naming conventions on transceiver breakout boards vary.

### Setup

1. **Plug the ESP32 into your Mac via USB**

2. **Verify it's detected:**
   ```bash
   make firmware-port
   ```
   Should print something like `/dev/cu.usbserial-210`. If empty, check the USB cable and try a USB-A adapter.

3. **Set WiFi credentials** (so the ESP32 joins your network):
   ```bash
   # Add to ~/.zshrc or equivalent
   export ESP32_WIFI_SSID="YourNetworkName"
   export ESP32_WIFI_PASS="YourPassword"
   ```
   If not set, the ESP32 creates its own AP: `ESP32-CAN` / `cancan12`

4. **Generate OTA credentials** (first time only):
   ```bash
   make ota-creds
   ```
   Generates random credentials and offers to persist them to `~/.zshrc`. These are used for TCP authentication and firmware signing.

5. **Build, test, and flash:**
   ```bash
   make flash
   ```
   This runs all tests, builds the firmware, and flashes it to the ESP32. It no longer auto-opens a monitor — run `make monitor` to see boot output (including the IP address), or `make capture` to capture the ESP32 serial stream **verbatim** to a timestamped notepad file (`timestamp_ms,raw_line` — every firmware line kept as-is, status text and hex-escaped noise bytes included; decode happens offline).

6. **Test the TCP connection:**
   ```bash
   nc <esp32-ip-address> 3333
   ```
   First line must be the AUTH token (printed by `make ota-creds`). Then type `ATZ` — should respond `ELM327 v2.3` with a `>` prompt.

### ESP32 Makefile Targets

| Target | Description |
|--------|-------------|
| `make firmware` | Build ESP32 firmware (auto-installs arduino-cli on first run) |
| `make flash` | Alias for `flash-over-usb` |
| `make flash-over-usb` | Test + build + flash to ESP32 over USB (no auto-monitor) |
| `make flash-over-tcp` | Sign + push firmware over WiFi to `ESP32_HOST` |
| `make monitor` | Open serial monitor at 115200 baud (live view) |
| `make capture` | Alias for `capture-usb` |
| `make capture-usb` | Capture the ESP32 serial stream to TWO timestamped files: `captures/<tag>_<timestamp>.raw.txt` (verbatim — every firmware line `timestamp_ms,raw_line`, frames/status/hex-escaped noise all kept, nothing dropped) AND `captures/<tag>_<timestamp>.csv` (parsed/filtered — one row per VALID frame `timestamp_ms,can_id,dlc,data_hex`; status/corrupt lines go to RAW only). `CAPFILE=Name` for a tag. The read loop is `select()`-gated (hard 0.5s timeout) so it never hangs on a quiet bus or a dropped USB link; a heartbeat prints to stderr every ~5s of silence. Ctrl-C to stop. |
| `make capture-tcp` | Same as `capture-usb` but connects over TCP to `ESP32_HOST` |
| `make reboot-over-tcp` | Reboot ESP32 over TCP |
| `make check-esp32` | Ping device, check TCP ports |
| `make firmware-port` | Show detected ESP32 serial port |

## BLE OBD2 Adapters

vehicle-sim also supports BLE OBD2 adapters (ELM327-based) as an alternative to the ESP32.

### Scan & Connect

```bash
./build-native/vehicle-sim --scan                          # List BLE devices
./build-native/vehicle-sim --connect <uuid> --vehicle tesla
```

### Tested Adapters

- Vgate iCar BLE (Nordic UART Service)
- OBDLink MX+ (proprietary service)
- Generic ELM327 BLE adapters (may vary)

## All CLI Options

```
--connect <target> Connect target: 'demo' or BLE adapter address (required)
--vehicle <type>   Vehicle type: tesla, audi_mlb_evo, generic, auto (required)
--scan             Scan for BLE OBD2 adapters
--list             List supported signals for each vehicle
--format <fmt>     Output format: json, csv, plain (default: plain)
--interval <ms>    Polling interval in ms (default: 500)
--log-csv <file>   Log CSV telemetry to file
--log-raw <file>   Log raw hex data to file
--help             show help
```

## Environment Variables

All credentials and configuration are read from environment variables — nothing is hardcoded in the repo. Add to `~/.zshrc` for persistence.

| Variable | Required | Description |
|----------|----------|-------------|
| `ESP32_WIFI_SSID` | For `make flash` | WiFi network name. If unset, ESP32 creates its own AP (`ESP32-CAN` / `cancan12`) |
| `ESP32_WIFI_PASS` | For `make flash` | WiFi password |
| `ESP32_HOST` | For `make flash-over-tcp`, `capture-tcp`, `reboot-over-tcp`, `check-esp32` | ESP32 IP address |
| `ESP32_TCP_TOKEN` | No (default: `vehicle-sim-2026`) | TCP auth token — required for all TCP connections (capture, reboot, OTA). Generated by `make ota-creds` and persisted in `~/.zshrc`. Baked into firmware at build time. |
| `OTA_KEYS_DIR` | No (default: `~/.vehicle-sim/ota`) | Directory containing Ed25519 signing keypair for firmware authenticity |
| `ESP32_PORT` | For `make flash` | Serial port path (e.g. `/dev/cu.usbserial-210`). Auto-detected if unset |
| `CAPTURE_VEHICLE` | No (default: `tesla`) | Vehicle type for capture |
| `CAPFILE` | No | Tag name for capture files |

**First-time setup:**
```bash
make ota-creds          # Generates random TCP auth token, offers to persist to ~/.zshrc
source ~/.zshrc         # Load token into current shell
make flash              # Flash ESP32 over USB (needs ESP32_PORT or auto-detect)
```

**Daily workflow:**
```bash
make check-esp32 ESP32_HOST=192.168.68.60   # Ping device, check TCP ports
make flash-over-tcp ESP32_HOST=192.168.68.60 # Sign + push firmware over WiFi
make capture-tcp ESP32_HOST=192.168.68.60   # Capture CAN frames over TCP
make reboot-over-tcp ESP32_HOST=192.168.68.60  # Reboot ESP32 over TCP
```

**Troubleshooting:**
```bash
make check-esp32 ESP32_HOST=192.168.68.60   # Is the device on the network?
# If unreachable: device may be in AP mode (ESP32-CAN) or bootlooping
# Re-flash over USB: make flash ESP32_WIFI_SSID=yourSSID ESP32_WIFI_PASS=...
```

## iOS App

The iOS app shows live vehicle telemetry on your iPhone.

### Prerequisites

- **Xcode** 16+ with iOS SDK
- Physical iPhone for device deployment (simulator works out of the box)

### Build & Deploy

```bash
make xcode         # Build native + icons, then open Xcode project
make ios           # Build Debug for simulator
make ios-signed    # Build Release for physical device
make deploy        # Install to connected iPhone
make run           # Install AND launch on device
```

> **Note**: The first time you run, Xcode may ask you to select a Development Team. Choose your Apple ID in Project Settings > Signing & Capabilities.

### iOS File Structure
```
vehicle-sim-ios/VehicleSim/
├── VehicleSimAppApp.swift        # SwiftUI app entry
├── ContentView.swift            # Telemetry display UI
├── VehicleViewModel.swift        # Connects wrapper to UI
├── VehicleSimWrapper.mm          # ObjC++ bridge (demo data generation)
├── VehicleSimWrapper.h          # ObjC header for Swift bridging
├── Info.plist                   # App config + BLE permissions
└── VehicleSimApp.xcodeproj/    # Xcode project (checked into git)
```

## All Makefile Targets

| Target | Description |
|--------|-------------|
| `make` | Build everything: native C++, tests, firmware, iOS |
| `make test` | Run C++ unit tests |
| `make native` `make macos` `make osx` | Build native C++ binary (`build-native/vehicle-sim`) |
| `make firmware` | Build ESP32 firmware |
| `make flash` | Alias for `flash-over-usb` |
| `make flash-over-usb` | Test + build + flash firmware to ESP32 over USB |
| `make flash-over-tcp` | Sign + push firmware over WiFi to `ESP32_HOST` |
| `make monitor` | Serial monitor at 115200 baud (live view) |
| `make capture` | Alias for `capture-usb` |
| `make capture-usb` | Capture ESP32 serial stream to `captures/<tag>_<timestamp>.raw.txt` (verbatim, nothing dropped) + `.csv` (parsed valid frames only); select-gated loop, never hangs; Ctrl-C to stop |
| `make capture-tcp` | Same as `capture-usb` but connects over TCP to `ESP32_HOST` |
| `make reboot-over-tcp` | Reboot ESP32 over TCP |
| `make check-esp32` | Ping device, check TCP ports |
| `make ios` | Build iOS app for simulator (Debug) |
| `make ios-signed` | Build signed Release for physical device |
| `make deploy` | Deploy to connected iPhone |
| `make run` | Deploy and launch on device |
| `make xcode` | Open in Xcode |
| `make install-deps` | Install Homebrew dependencies |
| `make update-dbc` | Update DBC files from opendbc |
| `make clean` | Clean build artifacts |
| `make scrub` | Full clean including ESP32 toolchain sentinel |
| `make help` | Show all targets |

## Important

**Always use `make` from the project root.** Never run `cmake` directly — the Makefile manages build directories (`build-native/`, `build-ios/`, `build-firmware/`).
