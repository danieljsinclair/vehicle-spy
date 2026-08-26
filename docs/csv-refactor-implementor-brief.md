# CSV Refactor — Implementor Brief

**Status:** Authoritative
**Date:** 2026-08-25
**Parent spec:** docs/csv-refactor-arch-spec.md

This brief is mechanically precise. Do NOT deviate. If you encounter a case not covered here, STOP and ask the critique architect.

---

## FILE 1: `include/vehicle-sim/telemetry/CsvRowFormatter.h`

**Replace the entire file with:**

```cpp
#pragma once

#include "vehicle-sim/telemetry/CsvTelemetryRow.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/telemetry/VehicleId.h"

#include <cstdint>
#include <optional>
#include <string>

namespace vehicle_sim::telemetry {

/**
 * Parameters for one CSV data row.
 *
 * Bundles the 13 decoded-telemetry columns into one object so csvRowLine()
 * takes a single parameter (cpp:S107) and the column order lives in exactly
 * one place (DRY). Adding a column = add a field here + one emit line in the
 * sink — no second copy to keep in sync.
 *
 * The four required fields (timestampMs, vehicleId, gearSelector,
 * dbcSignalCount) have no default initialisers: every producer must supply
 * them explicitly. The nine signal fields are optional<double> or
 * optional<int>; a nullopt renders as an empty cell ("not reported").
 */
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

/**
 * Single source of truth for the decoded-telemetry CSV schema (SRP, DRY).
 *
 * Both the file writer (TraceLogger) and the stdout writer (CsvStdoutSink)
 * render rows through this function, so a piped stdout stream and a `--log
 * <base>.csv` file are byte-identical by construction rather than by two
 * hand-maintained copies of the column list.
 *
 * Schema (13 columns):
 *   timestamp_ms, vehicle_id, speed_kmh, throttle_percent, brake_light,
 *   acceleration_g, steering_angle_deg, motor_rpm, motor_hv_voltage,
 *   motor_hv_current, motor_torque_nm, gear_selector, dbc_signal_count
 *
 * Doubles render with two decimal places; absent (nullopt) values render as
 * an empty cell. `brake_light` is a BINARY column: "1"/"0"/empty, never
 * decimal-formatted. `dbc_signal_count` counts the populated fields among
 * the 10 translated signal columns — timestamp and vehicle_id are not counted.
 *
 * Neither function appends a newline — the caller terminates the line.
 */

/// The CSV header line, without a trailing newline.
[[nodiscard]] std::string csvHeaderLine();

/**
 * How many of the 10 translated signal columns are populated in `signal`.
 *
 * Single source of truth for the count (DRY). `timestamp_ms` and `vehicle_id`
 * are NOT counted — only DBC-translated signals.
 */
[[nodiscard]] int countPopulated(const domain::VehicleSignal& signal);

/**
 * One CSV data row for `params`, without a trailing newline — the single
 * renderer for the whole schema.
 *
 * Taint note (cpp:S5145): cfamily's taint is origin-based — it fires on
 * uint64_t and int parameters too, not just std::string. It cannot be cleared
 * by type selection. The sanitization that matters is at the input boundary:
 * GearSelector and VehicleId producers pass through cli::forLog, which
 * replaces control bytes. The S5145 findings are resolved as false positives
 * via SonarCloud marking because cfamily's model cannot observe the cross-TU
 * sanitization. Do NOT attempt to "fix" S5145 by wrapping this output with
 * forLog() — that would corrupt the byte-identical CSV contract.
 */
[[nodiscard]] std::string csvRowLine(const CsvRowParams& params);

} // namespace vehicle_sim::telemetry
```

---

## FILE 2: `src/telemetry/CsvRowFormatter.cpp`

**Replace the entire file with:**

```cpp
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include <iomanip>
#include <optional>
#include <sstream>

namespace vehicle_sim::telemetry {

namespace {

// A present double renders with two decimal places; nullopt renders as an empty
// cell. The byte-identical contract needs this (absent vs. 0.00 are distinct).
std::string formatOptional(const std::optional<double>& value) {
    std::string rendered;
    if (value.has_value()) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << *value;
        rendered = oss.str();
    }
    return rendered;
}

// Binary column: "1"/"0"/empty — never decimal-formatted.
std::string formatBrakeLight(const std::optional<int>& brakeLight) {
    if (!brakeLight.has_value()) return {};
    return *brakeLight != 0 ? "1" : "0";
}

} // namespace

std::string csvHeaderLine() {
    return "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_light,"
           "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
           "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count";
}

int countPopulated(const domain::VehicleSignal& signal) {
    int populated = 0;
    if (signal.getThrottlePercent().has_value())  { ++populated; }
    if (signal.getSpeedKmh().has_value())         { ++populated; }
    if (signal.getAccelerationG().has_value())    { ++populated; }
    if (signal.getBrakeLight().has_value())       { ++populated; }
    if (signal.getSteeringAngleDeg().has_value()) { ++populated; }
    if (signal.getMotorRpm().has_value())         { ++populated; }
    if (signal.getMotorHvVoltage().has_value())   { ++populated; }
    if (signal.getMotorHvCurrent().has_value())   { ++populated; }
    if (signal.getMotorTorqueNm().has_value())    { ++populated; }
    if (signal.getGearSelector().has_value())     { ++populated; }
    return populated;
}

std::string csvRowLine(const CsvRowParams& params) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << params.timestampMs << ","
        << params.vehicleId.asString() << ","
        << formatOptional(params.speedKmh) << ","
        << formatOptional(params.throttlePercent) << ","
        << formatBrakeLight(params.brakeLight) << ","
        << formatOptional(params.accelerationG) << ","
        << formatOptional(params.steeringAngleDeg) << ","
        << formatOptional(params.motorRpm) << ","
        << formatOptional(params.motorHvVoltage) << ","
        << formatOptional(params.motorHvCurrent) << ","
        << formatOptional(params.motorTorqueNm) << ","
        << params.gearSelector.asString() << ","
        << params.dbcSignalCount;
    return oss.str();
}

} // namespace vehicle_sim::telemetry
```

---

## FILE 3: `src/telemetry/CsvStdoutSink.cpp`

**Replace the entire file with:**

```cpp
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/domain/Gear.h"

#include <optional>
#include <ostream>
#include <utility>

namespace vehicle_sim::telemetry {

CsvStdoutSink::CsvStdoutSink(std::ostream& out, const std::string& vehicleId)
    : out_(out)
    // The operator-supplied --vehicle label is converted to the validated
    // VehicleId type here: forLog sanitizes control bytes at this boundary.
    , vehicleId_(VehicleId::fromUserInput(vehicleId))
{
    writeHeader();
}

void CsvStdoutSink::operator()(const domain::VehicleSignal& signal) noexcept {
    writeRow(signal);
}

void CsvStdoutSink::writeHeader() {
    out_ << csvHeaderLine() << "\n";
    out_.flush();
}

void CsvStdoutSink::writeRow(const domain::VehicleSignal& signal) {
    // brake_light is a binary column: optional<bool> -> optional<int> (1/0/blank).
    std::optional<int> brakeLight;
    if (signal.getBrakeLight().has_value()) {
        brakeLight = *signal.getBrakeLight() ? 1 : 0;
    }

    // gear_selector renders as its display label (e.g. 4097 -> "D"), falling back
    // to the raw number for unmapped values — BUT only when the gear is reported.
    // A nullopt gear must render as an EMPTY cell (the "not reported" state),
    // distinct from a definite "N" (neutral). The label is a compile-time
    // constant or digit string — never external — so fromRegistry is correct.
    GearSelector gear;
    if (signal.getGearSelector().has_value()) {
        const auto gearLabel = domain::Gear::labelOr(
            *signal.getGearSelector(),
            std::to_string(*signal.getGearSelector()));
        gear = GearSelector::fromRegistry(gearLabel);
    }

    const CsvRowParams params{
        signal.getTimestampUtcMs(),
        vehicleId_,
        signal.getSpeedKmh(),
        signal.getThrottlePercent(),
        brakeLight,
        signal.getAccelerationG(),
        signal.getSteeringAngleDeg(),
        signal.getMotorRpm(),
        signal.getMotorHvVoltage(),
        signal.getMotorHvCurrent(),
        signal.getMotorTorqueNm(),
        gear,
        countPopulated(signal),
    };

    out_ << csvRowLine(params) << "\n";
    out_.flush();
}

void NullCsvStdoutSink::operator()(const domain::VehicleSignal& /*signal*/) noexcept {
    // Intentionally no-op — the disabled arm of the --stdout-csv branch.
}

std::unique_ptr<ICsvStdoutSink> createStdoutSink(bool enabled,
                                                 std::ostream& out,
                                                 const std::string& vehicleId) {
    std::unique_ptr<ICsvStdoutSink> sink;
    if (enabled) {
        sink = std::make_unique<CsvStdoutSink>(out, vehicleId);
    } else {
        sink = std::make_unique<NullCsvStdoutSink>();
    }
    return sink;
}

} // namespace vehicle_sim::telemetry
```

---

## FILE 4: `src/telemetry/TraceLogger.cpp`

**Replace the entire file with:**

```cpp
#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"

#include <optional>

namespace vehicle_sim::telemetry {

TraceLogger::TraceLogger(const std::string& filePath, const std::string& vehicleId)
    : file_(filePath)
    // The operator-supplied --vehicle label is converted to the validated
    // VehicleId type here: forLog sanitizes control bytes at this boundary.
    , vehicleId_(VehicleId::fromUserInput(vehicleId))
{
    if (!file_) {
        throw domain::TelemetryFileException(filePath);
    }
    writeHeader();
}

void TraceLogger::operator()(const domain::VehicleSignal& signal) noexcept {
    if (!file_.is_open()) {
        return;
    }
    writeRow(signal);
}

bool TraceLogger::isValid() const noexcept {
    return file_.is_open();
}

void TraceLogger::writeHeader() {
    file_ << csvHeaderLine() << "\n";
    file_.flush();
}

void TraceLogger::writeRow(const domain::VehicleSignal& signal) {
    // brake_light is a binary column: optional<bool> -> optional<int> (1/0/blank).
    std::optional<int> brakeLight;
    if (signal.getBrakeLight().has_value()) {
        brakeLight = *signal.getBrakeLight() ? 1 : 0;
    }

    // gear_selector renders as its display label (e.g. 4097 -> "D"), falling back
    // to the raw number for unmapped values — BUT only when the gear is reported.
    // A nullopt gear must render as an EMPTY cell (the "not reported" state),
    // distinct from a definite "N" (neutral). The label is a compile-time
    // constant or digit string — never external — so fromRegistry is correct.
    GearSelector gear;
    if (signal.getGearSelector().has_value()) {
        const auto gearLabel = domain::Gear::labelOr(
            *signal.getGearSelector(),
            std::to_string(*signal.getGearSelector()));
        gear = GearSelector::fromRegistry(gearLabel);
    }

    const CsvRowParams params{
        signal.getTimestampUtcMs(),
        vehicleId_,
        signal.getSpeedKmh(),
        signal.getThrottlePercent(),
        brakeLight,
        signal.getAccelerationG(),
        signal.getSteeringAngleDeg(),
        signal.getMotorRpm(),
        signal.getMotorHvVoltage(),
        signal.getMotorHvCurrent(),
        signal.getMotorTorqueNm(),
        gear,
        countPopulated(signal),
    };

    file_ << csvRowLine(params) << "\n";
    file_.flush();
}

} // namespace vehicle_sim::telemetry
```

---

## FILE 5: `src/cli/CsvReplayRunContext.cpp`

**Replace the `run()` function's emission block (lines 79-91) with:**

```cpp
        // No trailing newline: the stream is header + N rows, terminated by a
        // single newline after the final row so a downstream parser sees
        // exactly N records.
        if (!first) {
            out << "\n";
        }
        const CsvRowParams params{
            row.timestamp_ms,
            row.vehicle_id,
            std::optional<double>(row.speed_kmh),
            std::optional<double>(row.throttle_percent),
            row.brake_light,
            std::optional<double>(row.acceleration_g),
            std::optional<double>(row.steering_angle_deg),
            std::optional<double>(row.motor_rpm),
            std::optional<double>(row.motor_hv_voltage),
            std::optional<double>(row.motor_hv_current),
            std::optional<double>(row.motor_torque_nm),
            row.gear_selector,
            row.dbc_signal_count,
        };
        out << csvRowParams(params);
        out.flush();
```

**Also update the comment at line 64** from `// CSV DATA sink (csvRowLine(CsvTelemetryRow)).` to `// CSV DATA sink (csvRowLine(CsvRowParams)).`

---

## FILE 6: `src/cli/InteractiveRunContext.cpp`

**Replace the emission block (lines 45-57) with:**

```cpp
        const CsvRowParams params{
            row.timestamp_ms,
            row.vehicle_id,
            std::optional<double>(row.speed_kmh),
            std::optional<double>(row.throttle_percent),
            row.brake_light,
            std::optional<double>(row.acceleration_g),
            std::optional<double>(row.steering_angle_deg),
            std::optional<double>(row.motor_rpm),
            std::optional<double>(row.motor_hv_voltage),
            std::optional<double>(row.motor_hv_current),
            std::optional<double>(row.motor_torque_nm),
            row.gear_selector,
            row.dbc_signal_count,
        };
        out << csvRowLine(params) << "\n";
```

---

## FILE 7: `test/telemetry/CsvRowFormatter.test.cpp`

**Replace the `render()` helper (lines 48-61) with:**

```cpp
std::string render(const vehicle_sim::telemetry::CsvTelemetryRow& r) {
    vehicle_sim::telemetry::CsvRowParams params{
        r.timestamp_ms,
        r.vehicle_id,
        std::optional<double>(r.speed_kmh),
        std::optional<double>(r.throttle_percent),
        r.brake_light,
        std::optional<double>(r.acceleration_g),
        std::optional<double>(r.steering_angle_deg),
        std::optional<double>(r.motor_rpm),
        std::optional<double>(r.motor_hv_voltage),
        std::optional<double>(r.motor_hv_current),
        std::optional<double>(r.motor_torque_nm),
        r.gear_selector,
        r.dbc_signal_count,
    };
    return vehicle_sim::telemetry::csvRowLine(params);
}
```

**Replace the `DbcSignalCountCountsBrakeLight` test (lines 196-230) with:**

```cpp
TEST(CsvRowFormatterVehicleSignalTest, DbcSignalCountCountsBrakeLight) {
    const vehicle_sim::domain::VehicleSignal signal(
        vehicle_sim::domain::VehicleSignal::Params{
            .timestampUtcMs = 1ULL, .brakeLight = true});

    std::optional<int> brakeLight;
    if (signal.getBrakeLight().has_value()) {
        brakeLight = *signal.getBrakeLight() ? 1 : 0;
    }

    const vehicle_sim::telemetry::CsvRowParams params{
        signal.getTimestampUtcMs(),
        vehicle_sim::telemetry::VehicleId::fromUserInput(""),
        signal.getSpeedKmh(),
        signal.getThrottlePercent(),
        brakeLight,
        signal.getAccelerationG(),
        signal.getSteeringAngleDeg(),
        signal.getMotorRpm(),
        signal.getMotorHvVoltage(),
        signal.getMotorHvCurrent(),
        signal.getMotorTorqueNm(),
        vehicle_sim::telemetry::GearSelector::fromRegistry(
            vehicle_sim::domain::Gear::labelOr(
                signal.getGearSelector().value_or(0),
                std::to_string(signal.getGearSelector().value_or(0)))),
        vehicle_sim::telemetry::countPopulated(signal),
    };
    const std::string row = vehicle_sim::telemetry::csvRowLine(params);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), row);
    EXPECT_EQ(cells.at("brake_light"), "1");
    EXPECT_EQ(cells.at("dbc_signal_count"), "1");
}
```

---

## FILE 8: `test/io/FileCsvTelemetrySource.test.cpp`

**Replace the `renderRow()` helper (lines 49-62) with:**

```cpp
std::string renderRow(const vehicle_sim::telemetry::CsvTelemetryRow& r) {
    vehicle_sim::telemetry::CsvRowParams params{
        r.timestamp_ms,
        r.vehicle_id,
        std::optional<double>(r.speed_kmh),
        std::optional<double>(r.throttle_percent),
        r.brake_light,
        std::optional<double>(r.acceleration_g),
        std::optional<double>(r.steering_angle_deg),
        std::optional<double>(r.motor_rpm),
        std::optional<double>(r.motor_hv_voltage),
        std::optional<double>(r.motor_hv_current),
        std::optional<double>(r.motor_torque_nm),
        r.gear_selector,
        r.dbc_signal_count,
    };
    return vehicle_sim::telemetry::csvRowLine(params);
}
```

**Add this test at the end of the file (before the closing `} // namespace`):**

```cpp
// End-to-end sanitization: a control byte in the gear_selector cell of a CSV
// file must be replaced with '?' by the time the row is rendered. This closes
// the loop on the file-derived taint path (GearSelector::fromUserInput ->
// forLog at FileCsvTelemetrySource -> csvRowLine -> rendered output).
TEST(FileCsvTelemetrySourceTest, ControlCharInGearSelectorSanitizedOnRender) {
    TmpCsv f(
        "timestamp_ms,vehicle_id,gear_selector\n"
        "1000,tesla,a\x07b\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());
    ASSERT_TRUE(src.hasNext());
    auto row = src.next();
    EXPECT_EQ(row.gear_selector, "a?b");
    const auto rendered = renderRow(row);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), rendered);
    EXPECT_EQ(cells.at("gear_selector"), "a?b");
}
```

---

## VERIFICATION CHECKLIST

After all changes, run these commands. ALL must pass before you report done.

```bash
# 1. Build green
cmake --build build-native -j8

# 2. Tests green (1417+ pass)
./build-native/test/vehicle-sim-tests

# 3. CSV-specific tests pass
./build-native/test/vehicle-sim-tests --gtest_filter="*Csv*:*GearSelector*:*VehicleId*:*FileCsv*:*TraceLogger*"

# 4. Confirm S107 is gone (params-struct has 1 param, not 13)
grep -n "csvRowLine" src/telemetry/CsvRowFormatter.h
# Expected: "std::string csvRowLine(const CsvRowParams& params);"

# 5. Confirm S1238 is gone (const ref, not by-value)
grep -n "vehicleId" src/telemetry/CsvStdoutSink.cpp src/telemetry/TraceLogger.cpp
# Expected: "const std::string& vehicleId" in both constructors

# 6. Confirm no 13-param signature remains
grep -rn "csvRowLine(" src/ | grep -v "const CsvRowParams&"
# Expected: NO MATCHES

# 7. Confirm comments don't claim distinct types clear S5145
grep -rn "clears by construction\|removes.*from.*taint set\|no tainted input" src/telemetry/CsvRowFormatter.cpp include/vehicle-sim/telemetry/CsvRowFormatter.h
# Expected: NO MATCHES
```

---

## WHAT NOT TO DO

- Do NOT add `// NOSONAR` or `// NOLINT` to suppress S5145 — use SonarCloud false-positive marking instead.
- Do NOT wrap `csvRowLine` output with `forLog()` — that corrupts the byte-identical CSV contract.
- Do NOT add a second `csvRowLine` overload — there must be exactly one sink (DRY).
- Do NOT give `CsvRowParams` default initializers on the four required fields — every producer must supply them.
- Do NOT change `CsvTelemetryRow` — it stays as-is (gear_selector is already GearSelector type).
- Do NOT change `VehicleId` or `GearSelector` — they are correct.
- Do NOT change `FileCsvTelemetrySource` or `InteractiveCsvTelemetrySource` — they already sanitize correctly.
