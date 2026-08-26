# CSV Sink Refactor — Architectural Specification

**Status:** Authoritative (critique-arch-csv)
**Date:** 2026-08-25
**Replaces:** The implementor's 13-param `csvRowLine` (introduced S107)

---

## Problem

The live cfamily scan (build-sonar/sonar-report.json) shows 8 open findings:

| Rule | Location | Issue |
|------|----------|-------|
| S107 | CsvRowFormatter.cpp:59 | 13 params > 7 authorised |
| S5145 | CsvRowFormatter.cpp:17 | `formatOptional` double taint |
| S5145 | CsvRowFormatter.cpp:73 | `timestampMs` (uint64_t) |
| S5145 | CsvRowFormatter.cpp:74 | `vehicleId` (VehicleId distinct type) |
| S5145 | CsvRowFormatter.cpp:84 | `gearSelector` (GearSelector distinct type) |
| S5145 | CsvRowFormatter.cpp:85 | `dbcSignalCount` (int) |
| S1238 | CsvStdoutSink.cpp:12 | `vehicleId` by value |
| S1238 | TraceLogger.cpp:11 | `vehicleId` by value |

**Decisive evidence:** S5145 fires on `uint64_t` (L73) and `int` (L85) — pure numeric types that cannot be in any `std::string` taint set. S5145 also fires on `VehicleId` (L74) and `GearSelector` (L84), the distinct types invented specifically to escape `std::string` taint. **Taint is origin-based, not type-based. No code refactor can clear it.**

## Design Decisions

### 1. Params-struct replaces 13-param signature (clears S107 + DRY)

```cpp
struct CsvRowParams {
    std::uint64_t         timestampMs;
    VehicleId             vehicleId;
    std::optional<double> speedKmh;
    std::optional<double> throttlePercent;
    std::optional<int>    brakeLight;
    std::optional<double> accelerationG;
    std::optional<double> steeringAngleDeg;
    std::optional<double> motorRpm;
    std::optional<double> motorHvVoltage;
    std::optional<double> motorHvCurrent;
    std::optional<double> motorTorqueNm;
    GearSelector          gearSelector;
    int                   dbcSignalCount;
};

std::string csvRowLine(const CsvRowParams& params);
```

- 1 parameter → S107 cleared.
- Single column-order definition → DRY permanently fixed (my earlier finding #6).
- Adding a future column = one struct field + one emit line. No second copy to sync.
- No defaults on the four required fields (timestampMs, vehicleId, gearSelector, dbcSignalCount) — every producer must supply them explicitly.

### 2. S5145 resolved via SonarCloud false-positive marking (NOT code change)

The sanitization is already real and correct:
- `VehicleId::fromUserInput` / `GearSelector::fromUserInput` → `forLog()` at the producer boundary (FileCsvTelemetrySource.cpp:168-169, InteractiveCsvTelemetrySource.cpp:65-66)
- `validateVehicleLabel` at the CLI boundary for `--vehicle`

cfamily's origin-based model cannot observe the cross-TU sanitization. The findings are false positives in the only sense that matters: control bytes are severed before they reach the sink.

**Resolution:** Mark each of the 5 findings as **False Positive** in SonarCloud with justification:
> "Sanitized at input boundary via cli::forLog (GearSelector/VehicleId factories). cfamily origin-based taint cannot observe cross-TU sanitization. Control bytes are replaced before reaching this sink; byte-identical CSV contract preserved for all valid input."

Do NOT use `// NOSONAR` scattered across 5 lines — SonarCloud false-positive marking is auditable and documents the WHY at the finding site.

### 3. S1238 fixed (trivial, 2 free wins)

```cpp
// CsvStdoutSink.cpp:12
explicit CsvStdoutSink(std::ostream& out, const std::string& vehicleId = "");

// TraceLogger.cpp:11
explicit TraceLogger(const std::string& filePath, const std::string& vehicleId = "");
```

### 4. Comments rewritten (stop claiming distinct types clear S5145)

The current comments in `CsvRowFormatter.h:28-37` and `CsvRowFormatter.cpp:52-58` claim the distinct types "remove their fields from cfamily's std::string taint set" and that "the finding clears by construction." **This is empirically false** (scan L74, L84). Rewrite to:

> "Taint on this sink is origin-based (cfamily S5145 fires on uint64_t and int parameters), so it cannot be cleared by type selection. The sanitization that matters is at the input boundary: GearSelector and VehicleId producers pass through cli::forLog, which replaces control bytes. The S5145 findings are resolved as false positives via SonarCloud marking because cfamily's model cannot observe the cross-TU sanitization."

## Call-Site Changes

| File | Change |
|------|--------|
| `src/telemetry/CsvRowFormatter.h` | Add `CsvRowParams` struct; change `csvRowLine` signature to `const CsvRowParams&` |
| `src/telemetry/CsvRowFormatter.cpp` | Update sink to read from `params.*`; update comments |
| `src/telemetry/CsvStdoutSink.cpp` | Build `CsvRowParams` in `writeRow`; fix S1238 (`const std::string&`) |
| `src/telemetry/TraceLogger.cpp` | Build `CsvRowParams` in `writeRow`; fix S1238 (`const std::string&`) |
| `src/cli/CsvReplayRunContext.cpp` | Build `CsvRowParams` from `row.*` |
| `src/cli/InteractiveRunContext.cpp` | Build `CsvRowParams` from `row.*` |

## Test Changes

| File | Change |
|------|--------|
| `test/telemetry/CsvRowFormatter.test.cpp` | Update `render()` to build `CsvRowParams`; all existing assertions unchanged |
| `test/io/FileCsvTelemetrySource.test.cpp` | Update `renderRow()` to build `CsvRowParams`; all existing assertions unchanged |
| `test/cli/CsvReplayRunContext.test.cpp` | Add: file-round-trip test with control-char `gear_selector` → asserts "a?b" in rendered row |
| `test/cli/InteractiveRunContext.test.cpp` | No change needed (source is internal, no file path) |

## Exit Criteria

1. `cmake --build build-native` — green
2. `./build-native/test/vehicle-sim-tests` — 1417+ pass, 0 regressions
3. Sonar scan — 5 S5145 marked false-positive; S107 gone; S1238 gone
4. Byte-identical CSV contract preserved (existing pinned-string tests still pass)
5. `CsvRowParams` struct is the single column-order definition (DRY)
