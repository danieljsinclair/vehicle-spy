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
SN65HVD230 board    ESP32 DevKit
────────────────    ────────────
3.3V        ──────  3.3V
GND         ──────  GND
TX          ──────  GPIO 22  (TWAI TX)
RX          ──────  GPIO 21  (TWAI RX)

SN65HVD230 CANH  ────  OBD2 pin 6   (CAN-H)
SN65HVD230 CANL  ────  OBD2 pin 14  (CAN-L)
```

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
   export ESP32_WIFI_PASSWORD="YourPassword"
   ```
   If not set, the ESP32 creates its own AP: `ESP32-CAN` / `cancan12`

4. **Build, test, and flash:**
   ```bash
   make flash
   ```
   This runs all tests, builds the firmware, flashes it to the ESP32, and opens a serial monitor. The monitor shows the boot output including the IP address. Press `Ctrl-A` then `k` then `y` to quit the monitor.

5. **Test the TCP connection:**
   ```bash
   nc <esp32-ip-address> 3333
   ```
   Type `ATZ` and press Enter — should respond `ELM327 v2.3` with a `>` prompt.

### ESP32 Makefile Targets

| Target | Description |
|--------|-------------|
| `make firmware` | Build ESP32 firmware (auto-installs arduino-cli on first run) |
| `make flash` | Test + build + flash to ESP32 + open serial monitor |
| `make firmware-monitor` | Open serial monitor at 115200 baud |
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
| `make firmware` | Build ESP32 firmware |
| `make flash` | Test + build + flash firmware + serial monitor |
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
