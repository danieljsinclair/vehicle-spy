# iOS Build Instructions

## Prerequisites

- Xcode 16+ (command line tools installed)
- macOS 14+ (Sonoma) for iOS simulator builds
- CMake 3.20+

## Building with Make (Recommended)

From the vehicle-sim root directory:

```bash
# Build iOS simulator app
make ios
```

This will:
1. Build the C++ core library for iOS simulator (arm64, x86_64)
2. Build the vehicle-sim-ios app bundle
3. Place the output in `build-ios/`

## Manual Build via CMake + Xcode

```bash
# Generate Xcode project
mkdir build-ios && cd build-ios
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_IOS=ON -G Xcode ..

# Build for iOS simulator (arm64 Apple Silicon)
xcodebuild -scheme vehicle-sim-ios -configuration Release -sdk iphonesimulator -arch arm64

# Or build for iOS device (requires physical device, paid developer account)
xcodebuild -scheme vehicle-sim-ios -configuration Release -sdk iphoneos -arch arm64
```

The built app will be at:
```
build-ios/vehicle-sim-ios.app
```

## Testing in Simulator

```bash
# Launch the simulator and install the app
xcrun simctl install booted build-ios/vehicle-sim-ios.app
xcrun simctl launch booted com.axxiant.vehiclesim
```

Or double-click the `.app` bundle in Finder.

## Troubleshooting

### Code Signing Errors
Since we're using a free Apple ID, code signing may fail. Fix by:
1. Open the generated Xcode project in Xcode
2. Select the `vehicle-sim-ios` scheme
3. Go to Signing & Capabilities
4. Set "Team" to your personal Apple ID
5. Uncheck "Automatically manage signing" if needed
6. Set Provisioning Profile to "Automatic"

Alternatively, disable code signing for simulator builds (already configured).

### Missing Header Files
Ensure the C++ core library builds first:
```bash
make  # Build CLI and library
make ios
```

The iOS build depends on the core library headers in `include/vehicle-sim/`.

### Architecture Mismatch
If linking fails with "file was built for unsupported file format", ensure:
- iOS simulator build uses `-sdk iphonesimulator` (not `iphoneos`)
- Architectures match: `arm64` for Apple Silicon Macs, `x86_64` for Intel

### CoreBluetooth Unavailable
The iOS BLE implementation (`BLEManageriOS`) is currently a stub. For simulator testing, the mock implementation is used automatically.

## Next Steps

- Implement real `BLEManageriOS` using CoreBluetooth
- Add特斯拉 OBD2 service UUIDs and characteristic handling
- Stream real telemetry data to SwiftUI view model
- Add error handling and reconnection logic
