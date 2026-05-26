# Architecture

## DBC-Driven Vehicle Support

All CAN frame parsing and signal translation is driven by DBC (Database CAN) files. Adding a new vehicle requires **no protocol code, no frame parsing code, no translation code** — only:

1. A `.dbc` file in `resources/dbc/` (defines CAN IDs, byte positions, scaling, units, VAL_ tables)
2. A signal mapping in `DefaultVehicleConfigs.cpp` (maps DBC signal names → internal field names)
3. One registration line in `registerAll()`

The DBC parser handles CAN frame structure, byte ordering, factor/offset scaling, and unit conversion. Gear labels are translated automatically from DBC `VAL_` tables via `Gear::` constants. See `docs/adding-a-vehicle.md` for the full process.

## Data Flow

### CLI Demo Mode (`--connect demo`)
```
CLI args → parseArgs → validateOptions → resolveVehicleContext
  → SignalSourceFactory → DemoSignalSource → TelemetryRunner → display
```

### CLI BLE Mode (`--connect <address> --vehicle <type>`)
```
BLE → BLERunContext::run() → BLEConnectionManager → ELM327Transport
  → DBCTranslationService::processFrame() → DBCSignalTranslator → VehicleSignal → display
```

### CLI BLE Auto-Detect (`--connect <address> --vehicle auto`)
```
BLE → BLERunContext::runWithAutoDetection()
  → initializeForVINQuery (ATSP6, safe init) → queryVIN (Mode 09 PID 02)
  → decode WMI → VehicleDetector::makeToConfigId() → resolve vehicle config
  → reinit with correct protocol (CAN or OBD2)
  → runWithProtocol → normal BLE telemetry flow
```

### iOS Data Flow
```
ISignalSource (Strategy pattern) → VehicleSimWrapper (thin bridge)
  → DBCTranslationService::loadVehicleFromPath (NSBundle path resolution)
  → SwiftUI views
```

## Signal Source Abstraction
- `ISignalSource.h`: Abstract interface with `latestSignal()`, `start()`, `stop()`
- `DemoSignalSource`: Synthetic signal generation for testing
- `BLESignalSource`: Live BLE data with DBC translation (used internally by BLERunContext)

## CLI Orchestration Components
- `Orchestration`: `printBanner()`, `handleEarlyExit()`, `registerSignalHandlers()`, `resolveVehicleContext()`
- `CliOptions`: `parseArgs()`, `validateOptions()`, `printHelp()`, `printSupportedSignals()`
  - `isDemo()` / `isBLE()` helpers replace the old `source_type` field
- `SignalSourceFactory`: Factory returning ISignalSource for demo mode
- `VehicleConfigResolver`: Centralizes vehicle type validation, config lookup, protocol determination, DBC loading
- `TelemetryRunner`: Unified `run()` function taking ISignalSource via DI
- `BLERunContext`: BLE execution flow with auto-detection and health monitoring
  - `runWithAutoDetection()`: VIN query → WMI decode → config resolution
  - `runWithProtocol()`: Standard CAN or OBD2 telemetry loop

## DBC Pipeline
```
git submodule (commaai/opendbc) → resources/dbc/ → DBCParser → DBCSignalDefinition → DBCSignalMapper
```

`DBCTranslationService` has three loading methods:
- `loadVehicle()` — loads from config's relative DBC path
- `loadVehicleWithContent()` — loads from inline DBC string content
- `loadVehicleFromPath()` — loads from an absolute filesystem path (used by iOS NSBundle)

## Vehicle Auto-Detection

VIN-based detection flow (BLE only):
1. Connect BLE adapter, initialize ELM327 with safe ATSP6 (no protocol probing)
2. Send VIN query (`09 02`), wait for ELM327 prompt
3. `VehicleDetector::feedOBD2Frame()` extracts VIN from OBD2 response
4. `VehicleDetector::decodeWMI()` maps first 3 VIN chars → `VehicleMake`
5. `VehicleDetector::makeToConfigId()` maps make → registered config ID
6. Hard stop on failure — no silent fallback to "generic"

Passive CAN ID fingerprinting also runs during normal telemetry:
- `VehicleDetector::observeFrame()` maps CAN IDs → vehicle evidence
- Data-driven registry (OCP): add entries to `canIdRegistry_`, no conditional chains

## Gear Translation
- `Gear.h`: Canonical constants (PARK=-2, REVERSE=-1, NEUTRAL=0, AUTO_1=0x1001, etc.)
- `DBCSignalMapper::mapGearSignal()`: Translates raw CAN values → Gear constants via DBC VAL_ table
- Display layer maps constants to labels via `Gear::label()`

## ELM327 Transport

Two init sequences:
- `buildInitSequence()` — Standard OBD2 (uses ATSP0 auto-detect)
- `buildVINQueryInitSequence()` — VIN query only (uses ATSP6, no bus probing)
- `buildCANMonitorInitSequence()` — CAN monitor mode (7 commands: ATZ→ATCSM1→ATMA)

CAN monitor mode is read-only: `ATCSM1` enables silent monitoring (no CAN ACK bits on bus). All init sequences use prompt-driven pacing (`sendPromptDrivenSequence`) — waits for `>` prompt before each next command.

## Removed Components (dead code)
- `CANTranslatorBase`/`.h`, `CANSignalDecoderBase`/`.h`
- `AudiMLBTranslator`/`.h`, `TeslaCANTranslator`/`.h`
- `AudiSignalTranslator`/`.h`, `TeslaSignalTranslator`/`.h`
- `TeslaSignalParser`/`.h`, `SignalTranslatorFactory`/`.h`

All translation is now DBC-driven via `DBCSignalTranslator`.