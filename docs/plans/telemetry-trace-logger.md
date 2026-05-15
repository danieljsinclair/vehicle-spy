# Plan: Vehicle Telemetry Trace Logger + Raw CAN Capture

## Context

We need real vehicle traces to understand what data is actually available. The codebase has zero persistence — everything goes to stdout and is discarded. The `EventDispatcher` exists but is unwired. The existing Tesla signal parsing is mock/placeholder — the real data is on the CAN bus, and opendbc has the DBC files.

**Goal**: Capture two parallel streams — decoded signal CSV (what our translators produce) and raw CAN frame hex dump (everything on the wire for offline analysis). Mac CLI first. Flags off by default.

## Canonical CSV Schema (translated signals)

One schema across all sources. Empty cells where no data. No zeros unless actually zero. No calculated fields.

```
timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,motor_rpm,gear_selector,motor_torque_nm
```

| Column | Type | Notes |
|--------|------|-------|
| `timestamp_utc_ms` | uint64 | Unix ms from system clock at capture time |
| `throttle_pct` | double | 0.0–100.0 accelerator pedal position |
| `speed_kmh` | double | Vehicle speed |
| `acceleration_g` | double | Only if sourced from vehicle — leave empty if derived |
| `brake_pct` | double | Brake pedal/pressure if available |
| `motor_rpm` | double | Motor RPM |
| `gear_selector` | string | P/R/N/D — not a gear number |
| `motor_torque_nm` | double | Actual motor torque (Nm) |

Empty cell = no data available from this source. No zero-filling.

## Raw CAN Capture Format

Separate file from the CSV. Text-based hex dump for immediate readability and greppability:

```
# vehicle-sim raw CAN capture - started 2026-05-13T14:20:00Z
# format: timestamp_utc_ms,can_id,dlc,data_hex
1746849600123,118,8,3C00180004A001FF
1746849600124,257,8,8019400000000000
1746849600125,145,8,0000000000000000
```

At ~50 active CAN IDs × 50Hz = ~2500 lines/sec → ~25MB/hour. Trivial on Mac.

**Why text not binary pcap**: Human-readable, greppable, Python/pandas loadable, no binary framing to corrupt. Can convert to pcap later if needed.

## CLI Flags (independent, off by default)

```
--log-csv <file>     Translated signal CSV (via EventDispatcher consumer)
--log-raw <file>     Raw CAN frame hex dump (tapped before translation)
```

Both can run simultaneously or independently. Mac CLI is v1 target. iPhone/ESP32 not prohibited but not v1.

## Architecture

### Two capture points in the pipeline

```
BLE/CAN Hardware
    |
    v
onDataReceived callback (raw bytes)
    |
    +---> RawTraceLogger.write(data)          # --log-raw
    |
    v
SignalTranslator / SignalParser (decode to VehicleSignal)
    |
    v
EventDispatcher.dispatch(VehicleSignal)
    |
    +---> TraceLogger(signal)                  # --log-csv
    +---> StdoutDisplay(signal)                # existing print
```

- **RawTraceLogger**: Captures raw byte arrays with timestamps BEFORE translation. Registered directly in the `onDataReceived` callback.
- **TraceLogger**: Captures translated `VehicleSignal` AFTER translation. Registered as an `EventDispatcher` consumer.

### EventDispatcher wiring (Option B)

Wire the existing `EventDispatcher` into the connect/simulate flows:
1. Create `EventDispatcher` instance
2. Register `TraceLogger` as consumer (if `--log-csv` specified)
3. Register stdout display as consumer (replaces inline `printTelemetryRow`)
4. In `onDataReceived`: write raw bytes to `RawTraceLogger` (if `--log-raw`), then parse/translate, then `dispatcher.dispatch(signal)`

## Implementation

### New files

1. **`include/vehicle-sim/telemetry/TraceLogger.h`** — CSV signal logger
2. **`src/telemetry/TraceLogger.cpp`** — CSV implementation
3. **`include/vehicle-sim/telemetry/RawTraceLogger.h`** — raw hex dump logger
4. **`src/telemetry/RawTraceLogger.cpp`** — raw capture implementation
5. **`test/telemetry/TraceLogger.test.cpp`** — tests

### TraceLogger (CSV)

```cpp
namespace vehicle_sim::telemetry {

class TraceLogger {
public:
    explicit TraceLogger(std::string filePath);
    ~TraceLogger();

    void operator()(const domain::VehicleSignal& signal);

    TraceLogger(const TraceLogger&) = delete;
    TraceLogger& operator=(const TraceLogger&) = delete;
    TraceLogger(TraceLogger&&) noexcept;
    TraceLogger& operator=(TraceLogger&&) noexcept;

private:
    void writeHeader();
    void writeRow(const domain::VehicleSignal& signal);
    std::string formatDouble(double value);  // empty string if NaN/default

    std::ofstream file_;
};

}
```

- `operator()` usable as `EventDispatcher::SignalCallback`
- Constructor writes header row, opens file
- `formatDouble` — returns empty string for unset values, formatted value otherwise
- Empty cells for fields not populated by current source

### RawTraceLogger (hex dump)

```cpp
namespace vehicle_sim::telemetry {

class RawTraceLogger {
public:
    explicit RawTraceLogger(std::string filePath);
    ~RawTraceLogger();

    void write(std::uint64_t timestampMs, const std::vector<std::uint8_t>& data);

    RawTraceLogger(const RawTraceLogger&) = delete;
    RawTraceLogger& operator=(const RawTraceLogger&) = delete;
    RawTraceLogger(RawTraceLogger&&) noexcept;
    RawTraceLogger& operator=(RawTraceLogger&&) noexcept;

private:
    std::ofstream file_;
};

}
```

- `write()` formats: `timestamp,can_id_or_raw_hex` — writes raw data as hex string with timestamp
- Called directly from `onDataReceived` before any parsing/translation
- Simple append, no buffering (let the OS handle write coalescing)

### main.cpp changes

1. Add `--log-csv <file>` and `--log-raw <file>` CLI flags
2. Create `EventDispatcher` in connect/simulate scopes
3. If `--log-csv`: create `TraceLogger`, register on dispatcher
4. Register stdout display as dispatcher consumer
5. If `--log-raw`: create `RawTraceLogger`, call in `onDataReceived` before parsing
6. Refactor connect callback: raw capture → parse → `dispatcher.dispatch()`

### CMake changes

Add to `VEHICLE_SIM_LIB_SOURCES`:
- `src/telemetry/TraceLogger.cpp`
- `src/telemetry/RawTraceLogger.cpp`

## VehicleSignal expansion (in this PR)

Add three fields to `VehicleSignal`:

| New field | Type | Range | Clamp |
|-----------|------|-------|-------|
| `motorRpm` | `double` | 0.0–20000.0 | `std::clamp(val, 0.0, 20000.0)` |
| `gearSelector` | `std::string` | "P","R","N","D","S" or empty | stored as-is |
| `motorTorqueNm` | `double` | -750.0–750.0 | `std::clamp(val, -750.0, 750.0)` |

### Files to change

| File | Change |
|------|--------|
| `include/vehicle-sim/domain/VehicleSignal.h` | Add constructor params, getters, private members |
| `src/domain/VehicleSignal.cpp` | Update constructor, clamp new fields, update `operator==` |
| `src/domain/OBD2SignalTranslator.cpp` | Pass empty/default values for new fields (OBD2 doesn't provide them yet) |
| `src/domain/TeslaSignalTranslator.cpp` | Pass empty/default values (current mock format doesn't carry them) |
| `src/domain/TeslaSignalParser.cpp` | Pass empty/default values |
| `src/VehicleSim.cpp` | Pass empty/default values in simulator signal construction |
| `test/domain/VehicleSignal.test.cpp` | Add test cases for new fields |
| `test/domain/OBD2SignalTranslator.test.cpp` | Update existing tests (new constructor params) |
| `test/domain/TeslaSignalParser.test.cpp` | Update existing tests |
| `test/domain/EventDispatcher.test.cpp` | Update existing tests |
| `test/domain/SignalTranslatorFactory.test.cpp` | Update existing tests |
| `test/integration/EventDispatcherIntegration.test.cpp` | Update existing tests |
| `test/VehicleSimulator.test.cpp` | Update existing tests |
| `vehicle-sim-ios/VehicleSim/VehicleSimWrapper.h` | Add new Obj-C properties |
| `vehicle-sim-ios/VehicleSim/VehicleSimWrapper.mm` | Wire new getters |
| `vehicle-sim-ios/VehicleSim/VehicleViewModel.swift` | Add new @Published vars |
| `vehicle-sim-ios/VehicleSim/ContentView.swift` | Display new fields (if UI space permits) |

Constructor uses default parameter values so existing call sites don't break:
```cpp
VehicleSignal(
    double throttlePercent,
    double speedKmh,
    double accelerationG,
    double brakePercent,
    std::uint64_t timestampUtcMs,
    double motorRpm = 0.0,
    std::string gearSelector = "",
    double motorTorqueNm = 0.0
) noexcept;
```

## Key existing files

| File | Role |
|------|------|
| `include/vehicle-sim/domain/EventDispatcher.h` | Pub/sub bus (implemented, unwired) |
| `src/domain/EventDispatcher.cpp` | Thread-safe dispatch |
| `include/vehicle-sim/domain/VehicleSignal.h` | Canonical signal (5 fields) |
| `src/main.cpp:253-287` | Connect callback (refactor target) |
| `src/ble/BLEManagerBase.h:22-35` | BLE UUIDs (NUS for ELM327) |
| `include/vehicle-sim/domain/TeslaSignalTranslator.h` | Mock Tesla format (placeholder) |
| `include/vehicle-sim/domain/TeslaSignalParser.h` | Alternative CAN parser (placeholder) |
| `docs/RESEARCH_TESLA_CAN_BLE.md` | Existing Tesla research notes |

## Files to modify

| File | Change |
|------|--------|
| `include/vehicle-sim/telemetry/TraceLogger.h` | **NEW** |
| `src/telemetry/TraceLogger.cpp` | **NEW** |
| `include/vehicle-sim/telemetry/RawTraceLogger.h` | **NEW** |
| `src/telemetry/RawTraceLogger.cpp` | **NEW** |
| `test/telemetry/TraceLogger.test.cpp` | **NEW** |
| `src/main.cpp` | Add `--log-csv`/`--log-raw` flags, wire EventDispatcher, raw capture |
| `CMakeLists.txt` | Add new source files |
| `test/CMakeLists.txt` | Add test file |

## Team Setup

### Roles (user's established team pattern)

| Role | Agent Type | Responsibility |
|------|-----------|----------------|
| **team-lead** | claude (default) | Delegation and orchestration ONLY. No hands-on code. Assigns tasks, reviews output, integrates. |
| **critic** | general-purpose | Reviews ALL code for SOLID (SRP, DI, OCP), DRY, separation of concerns. Reviews after each implementation task. |
| **test-architect** | general-purpose | Reviews tests separately from implementation. Ensures tests add real business value, not coverage vanity. |
| **implementer** | general-purpose | Writes production code. Handed specific file paths and specs. |

### Rules (from established project conventions)

- Team lead does NOT edit files, run builds, or write code
- Critic reviews ALL code — SOLID, DRY, separation of concerns
- Test architect and implementer are ALWAYS different agents
- Tests written blind to acceptance criteria, not informed by implementation
- RED phase tests MUST compile; only GREEN tests committed
- Tests assert correct behaviour, not fragile error messages
- Do NOT kill unresponsive agents — alert user, continue with others

### Task breakdown

**Task 1: Expand VehicleSignal** (implementer)
- Add `motorRpm`, `gearSelector`, `motorTorqueNm` to VehicleSignal
- Default params so existing call sites compile
- Update translators to pass defaults
- Blocked by: nothing

**Task 2: Implement RawTraceLogger** (implementer — can run in parallel with Task 1)
- New files: `include/vehicle-sim/telemetry/RawTraceLogger.h`, `src/telemetry/RawTraceLogger.cpp`
- Hex dump format: `timestamp_utc_ms,data_hex`
- Blocked by: nothing

**Task 3: Implement TraceLogger** (implementer)
- New files: `include/vehicle-sim/telemetry/TraceLogger.h`, `src/telemetry/TraceLogger.cpp`
- CSV schema: `timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,motor_rpm,gear_selector,motor_torque_nm`
- Empty cells for unset values
- Blocked by: Task 1 (needs expanded VehicleSignal)

**Task 4: Write tests** (test-architect — different agent from implementer)
- TraceLogger tests: write known signal, read CSV, assert header + row
- RawTraceLogger tests: write known bytes, read hex, assert format
- VehicleSignal tests: new fields, clamping, defaults
- Blocked by: Tasks 1, 2, 3

**Task 5: Critic review** (critic)
- Review all new and modified code for SOLID, DRY, separation of concerns
- Review that TraceLogger/RawTraceLogger follow SRP
- Review that VehicleSignal remains immutable value object
- Review that EventDispatcher integration doesn't violate OCP
- Blocked by: Tasks 1, 2, 3

**Task 6: Wire EventDispatcher + CLI flags** (implementer)
- Add `--log-csv <file>` and `--log-raw <file>` to main.cpp
- Wire EventDispatcher into connect/simulate flows
- Register consumers (TraceLogger, stdout display, RawTraceLogger)
- Update CMakeLists.txt and test/CMakeLists.txt
- Blocked by: Tasks 1, 2, 3

**Task 7: Update iOS bridge** (implementer)
- Add new VehicleSignal fields to VehicleSimWrapper.h/.mm
- Add @Published vars to VehicleViewModel.swift
- Update ContentView.swift if space permits
- Blocked by: Task 1

### Launch instructions

```
1. Create team: "telemetry-trace"
2. Create tasks 1-7 with dependencies above
3. Spawn 4 agents:
   - "implementer" (general-purpose) — claims Tasks 1, 2, then 3, 6, 7 in dependency order
   - "test-architect" (general-purpose) — claims Task 4 when unblocked
   - "critic" (general-purpose) — claims Task 5 when unblocked
   - "team-lead" (claude/default) — orchestrates, does NOT code
4. Team lead assigns tasks, reviews agent output, integrates
5. Critic gets final say before any code is considered done
```

### Verification

1. **Unit tests**: `ctest --output-on-failure` — all pass
2. **Simulate mode**: `vehicle-sim --simulate --log-csv trace.csv --log-raw raw.log` for 10s
3. **Inspect CSV**: header + ~20 rows, empty cells for fields simulator doesn't populate
4. **Inspect raw**: hex dump lines with timestamps
5. **Real device**: `vehicle-sim --connect <addr> --log-csv tesla.csv --log-raw tesla_raw.log`
6. **iOS build**: Xcode build succeeds with new VehicleSignal fields
