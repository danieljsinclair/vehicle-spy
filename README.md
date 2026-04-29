# vehicle-sim

Real-time Tesla Model Y OBD2 telemetry display system.

Connect a BLE OBD2 adapter to your Tesla's OBD-II port and see throttle position, speed, RPM, and more — either on macOS CLI or on your iPhone.

## Building

### Prerequisites

- macOS with Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.20+

### Build & Test

```bash
make            # Build native macOS binary
make test       # Build and run all tests (88 passing)
make clean      # Clean build artifacts
make help       # Show all targets
```

Binary: `build-native/vehicle-sim`

## Quick Start

### 1. Demo (No Hardware)

```bash
./build-native/vehicle-sim --simulate
```

Displays mock Tesla telemetry at 10Hz. Good for verifying the display pipeline.

### 2. Scan for BLE OBD2 Adapters

```bash
./build-native/vehicle-sim --scan
```

Scans for 10 seconds and lists all discoverable BLE devices. Your OBD2 adapter (e.g., Vgate iCar, OBDLink MX+) will appear in the list when powered and in range.

### 3. Connect to Your Tesla

```bash
# Use the UUID from --scan output
./build-native/vehicle-sim --connect <device-uuid>

# Faster polling (200ms intervals)
./build-native/vehicle-sim --connect <device-uuid> --interval 200
```

On connection the CLI will:
1. Discover BLE services and characteristics
2. Initialize the ELM327 adapter (AT commands)
3. Start polling OBD2 PIDs (throttle, speed, RPM, engine load, coolant temp)
4. Display parsed telemetry in real-time

## Hardware Setup

1. **Plug** your BLE OBD2 adapter into the Tesla's OBD-II port (under the steering column)
2. **Power on** the Tesla (accessory mode minimum; drive mode recommended)
3. **Scan** from CLI — the adapter should appear within ~10 seconds
4. **Connect** using the UUID from scan output

### Tested Adapters

- Vgate iCar BLE (uses Nordic UART Service)
- OBDLink MX+ (uses proprietary service)
- Generic ELM327 BLE adapters (may vary)

> The Tesla OBD-II port is only powered when the vehicle is ON. Some adapters require the vehicle to be in Drive mode to send data.

## All CLI Options

```
--scan             Scan for BLE OBD2 adapters
--connect <addr>   Connect to adapter (use UUID from scan)
--list             List supported Tesla signals
--simulate         Demo mode with mock telemetry
--format <fmt>     Output format: json, csv, plain (default: plain)
--interval <ms>    Polling interval in ms (default: 500)
--help             Show help
```

## iOS App

The iOS app shows live Tesla telemetry on your iPhone.

### Prerequisites

- **Xcode** 16+ with iOS SDK
- **Homebrew** + **ImageMagick** for app icons:
  ```bash
  brew install imagemagick
  ```
- Physical iPhone for device deployment (simulator works out of the box)

### Quick Start

1. **Install dependencies** (once):
   ```bash
   make install-deps          # Installs ImageMagick, cmake, etc. via Homebrew
   ```

2. **Build everything** (native C++ libs + iOS app icons):
   ```bash
   make                       # Builds native libs + tests + iOS simulator build
   ```

3. **Run on iPhone Simulator**:
   ```bash
   make ios                   # Builds Debug for simulator
   # Then in Xcode: select iPhone simulator → press Play
   ```

4. **Deploy to physical iPhone**:
   ```bash
   make ios-signed            # Build Release-signed .app for device
   make deploy                # Install to connected iPhone
   make run                   # Install AND launch on device
   ```

   > **Note**: The first time you run, Xcode may ask you to select a Development Team. Choose your Apple ID in Project Settings → Signing & Capabilities.

5. **Open in Xcode** (for development/debugging):
   ```bash
   make xcode                 # Builds native + icons, then opens .xcodeproj
   ```

### Makefile Targets

| Target | Description |
|--------|-------------|
| `make ios` | Build Debug for iOS Simulator |
| `make ios-signed` | Build Release for physical device |
| `make deploy` | Install signed build to connected iPhone |
| `make run` | Install and launch on device |
| `make xcode` | Open Xcode project (ensures native libs are built) |
| `make native` | Build C++ native libraries only |
| `make test` | Run C++ unit tests |
| `make scrub` | Full clean: DerivedData, caches, generated icons |

### Notes

- **Native library dependency**: `ios`, `ios-signed`, and `xcode` all depend on `native` — C++ libs are built automatically.
- **App icons**: Generated from `image/ODB2_car_logo*.png` via ImageMagick; regenerated when source changes.
- **Full-screen mode**: Enabled for both Debug and Release via `UIRequiresFullScreen` in Info.plist.
- **Scheme configuration**: The Xcode scheme uses `BuildableProductRunnable` — automatically picks correct build (simulator vs device) when you press Play.

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

**Note**: The Xcode project references headers at `../../../include` — you must run `make ios` before building in Xcode.

```
src/ble/
  BLEManager.cpp              # Platform factory
  BLEManagerBase.cpp          # OBD2 protocol (PIDs, parsing, ELM327 init)
  platform/BLEManagerMacOS.mm # CoreBluetooth implementation
  platform/BLEManagerMock.cpp # Mock for testing
include/vehicle-sim/
  domain/VehicleSignal.h      # Immutable telemetry value object
  domain/TeslaSignalParser.h  # CAN bus signal parser
  domain/EventDispatcher.h    # Thread-safe event routing
  ble/BLEManagerBase.h        # OBD2 UUIDs, PID constants, shared logic
test/
  ble/BLEManager.test.cpp     # Unit tests
  integration/                # Integration tests (EventDispatcher, etc.)
```

## Important

**Always use `make` from the project root.** Never run `cmake` directly — the Makefile manages build directories (`build-native/`, `build-ios/`).
