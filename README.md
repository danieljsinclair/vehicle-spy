# vehicle-sim

High integrity C++20 automotive telemetry simulator and real-time data capture system for Tesla Model Y.

## Product Vision
> Build the most reliable, testable open-source vehicle telemetry stack for research and diagnostic use. No black boxes. No magic. Full transparency.

## Core Capabilities
1. Real-time BLE OBD2 data capture from Tesla Model Y
2. Physics simulation engine for throttle, speed, torque, RPM, acceleration
3. Async non-blocking API with both polling and event-driven interfaces
4. Strict SOLID architecture with 100% dependency injection
5. Cross platform support: macOS, iOS, Linux, ESP32

## Building

### Prerequisites

- CMake 3.20 or higher
- C++17 compatible compiler
- See README for platform-specific dependencies

### Build Instructions

```bash
git clone <repository-url>
cd vehicle-sim
make
```

The `vehicle-sim` executable will be created in the `build` directory.

## Usage

```bash
# Run with default settings
./build/vehicle-sim --help

# Example usage
./build/vehicle-sim --engine default --duration 10
```

## Project Structure

```
vehicle-sim/
├── Makefile           # Build entry point (use this!)
├── CMakeLists.txt     # CMake configuration
├── src/              # Source code organized by concern
│   ├── config/      # Configuration handling
│   ├── simulation/  # Simulation core
│   ├── presentation/# Output/presentation layer
│   └── ...
├── include/         # Public headers
├── test/            # Unit and integration tests
├── docs/            # Documentation
└── build/           # Build directory (created by make)
```

## Important

**Always use `make` from the project root.** Never run `cmake` directly, as this will overwrite the Makefile wrapper and break the build system.
