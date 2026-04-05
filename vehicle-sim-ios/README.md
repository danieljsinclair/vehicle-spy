# VehicleSim iOS App

SwiftUI application for real-time Tesla telemetry display.

## Structure

```
VehicleSim/
├── VehicleSim.xcodeproj/    # Xcode project
├── VehicleSim/              # Swift sources
│   ├── AppDelegate.swift
│   ├── ContentView.swift
│   ├── VehicleViewModel.swift
│   └── Models/
│       └── TelemetryData.swift
├── Supporting Files/
│   └── Info.plist
└── Tests/                   # Unit tests
```

## Building

```bash
# From vehicle-sim root:
make ios
# or open VehicleSim.xcodeproj in Xcode
```

## Requirements

- Xcode 16+ (iOS 15+ SDK)
- macOS 14+ for simulator builds
- vehicle-sim core library built for iOS simulator
