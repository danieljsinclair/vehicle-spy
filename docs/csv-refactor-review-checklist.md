# CSV Refactor — Review Checklist

**Date:** 2026-08-25
**Parent:** docs/csv-refactor-arch-spec.md, docs/csv-refactor-implementor-brief.md

Use this checklist when reviewing the implementor's diff. Reject the diff if ANY item fails.

---

## STRUCTURAL CHECKS

- [ ] `CsvRowParams` struct exists in `include/vehicle-sim/telemetry/CsvRowFormatter.h`
- [ ] `CsvRowParams` has exactly 13 fields, no defaults on the 4 required ones
- [ ] `csvRowLine` takes `const CsvRowParams&` (1 param, not 13)
- [ ] No second `csvRowLine` overload exists
- [ ] `CsvTelemetryRow` is unchanged (gear_selector still GearSelector type)
- [ ] `VehicleId` and `GearSelector` are unchanged
- [ ] `FileCsvTelemetrySource` and `InteractiveCsvTelemetrySource` are unchanged

## S107 VERIFICATION

- [ ] `csvRowLine` signature: `std::string csvRowLine(const CsvRowParams& params)`
- [ ] No function in the diff has > 7 parameters
- [ ] Build is green: `cmake --build build-native -j8`

## S1238 VERIFICATION

- [ ] `CsvStdoutSink` constructor: `const std::string& vehicleId`
- [ ] `TraceLogger` constructor: `const std::string& vehicleId`
- [ ] No by-value `std::string` parameters remain in the changed files

## S5145 RESOLUTION

- [ ] No `// NOSONAR` or `// NOLINT` added to suppress S5145
- [ ] No `forLog()` wrapping of `csvRowLine` output
- [ ] Comments in CsvRowFormatter.h/cpp do NOT claim distinct types clear S5145
- [ ] Comments correctly state taint is origin-based and resolved via SonarCloud marking

## BYTE-IDENTICAL CONTRACT

- [ ] Column order unchanged: timestamp_ms, vehicle_id, speed_kmh, throttle_percent, brake_light, acceleration_g, steering_angle_deg, motor_rpm, motor_hv_voltage, motor_hv_current, motor_torque_nm, gear_selector, dbc_signal_count
- [ ] `std::fixed << std::setprecision(2)` still applied to all double columns
- [ ] `formatBrakeLight` still renders "1"/"0"/empty (not decimal)
- [ ] `formatOptional` still renders nullopt as empty cell
- [ ] `countPopulated` still counts 10 DBC signals (not timestamp/vehicle_id)
- [ ] No trailing newline added by `csvRowLine`

## TEST COVERAGE

- [ ] All 1417+ existing tests pass
- [ ] `CsvRowFormatter.test.cpp` updated to use `CsvRowParams`
- [ ] `FileCsvTelemetrySource.test.cpp` updated to use `CsvRowParams`
- [ ] New test: control-char gear_selector file-round-trip ("a\x07b" -> "a?b")
- [ ] `CsvReplayRunContext.test.cpp` and `InteractiveRunContext.test.cpp` pass unchanged

## CALL-SITE CHECKS

- [ ] `CsvStdoutSink::writeRow` builds `CsvRowParams` and calls `csvRowLine(params)`
- [ ] `TraceLogger::writeRow` builds `CsvRowParams` and calls `csvRowLine(params)`
- [ ] `CsvReplayRunContext::run` builds `CsvRowParams` from `row.*`
- [ ] `InteractiveRunContext::runImpl` builds `CsvRowParams` from `row.*`
- [ ] All 4 call sites use designated initializers (field names, not positional)

## RED FLAGS (REJECT IF PRESENT)

- [ ] No new `csvRowLine` overload
- [ ] No `forLog()` wrapping of sink output
- [ ] No `// NOSONAR` or `// NOLINT` for S5145
- [ ] No change to `VehicleId` or `GearSelector`
- [ ] No change to `CsvTelemetryRow`
- [ ] No change to `FileCsvTelemetrySource` or `InteractiveCsvTelemetrySource`
- [ ] No default initializers on `CsvRowParams` required fields
- [ ] No second column-order definition (DRY)
