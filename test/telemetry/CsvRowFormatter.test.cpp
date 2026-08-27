// CsvRowFormatter.test.cpp — the decoded-CSV schema's single renderer.
//
// Business value: the 13-column --stdout-csv schema is rendered by ONE function
// (csvRowLine, 13 params) so a piped stdout stream, a --log file, a CSV replay,
// and an interactive session are byte-identical by construction. These tests pin
// that renderer (field order, 2-decimal precision, binary brake_light) and the
// validated-input types (VehicleId, GearSelector) that keep the sink clean.

#include "vehicle-sim/telemetry/CsvRowFormatter.h"
#include "vehicle-sim/telemetry/CsvCell.h"
#include "vehicle-sim/telemetry/GearSelector.h"
#include "vehicle-sim/telemetry/VehicleId.h"
#include "vehicle-sim/domain/Gear.h"

#include "telemetry/CsvShape.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>

namespace {

// A fully-populated sample row. gear_selector holds the rendered label "D"
// (the CsvTelemetryRow overload emits gear_selector RAW — it is the producer's
// job to supply the label, not the sink's).
vehicle_sim::telemetry::CsvTelemetryRow sampleRow() {
    vehicle_sim::telemetry::CsvTelemetryRow r;
    r.timestamp_ms       = 1000;
    r.vehicle_id         = vehicle_sim::telemetry::VehicleId::fromUserInput("tesla");
    r.speed_kmh          = 50.0;
    r.throttle_percent   = 30.0;
    r.brake_light        = 1;
    r.acceleration_g     = 0.15;
    r.steering_angle_deg = 2.5;
    r.motor_rpm          = 3000.0;
    r.motor_hv_voltage   = 400.0;
    r.motor_hv_current   = 60.0;
    r.motor_torque_nm    = 450.0;
    r.gear_selector      = vehicle_sim::telemetry::GearSelector::fromUserInput("D");
    r.dbc_signal_count   = 42;
    return r;
}

// Render a CsvTelemetryRow through the single-param params-struct sink (SRP:
// every path routes through here). Plain doubles are wrapped as present optionals
// (the row type's 0.0 default must render as "0.00", matching the old raw-<<
// rendering byte-for-byte).
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

} // namespace

// ================================================
// Schema shape (header/field count)
// ================================================

TEST(CsvRowFormatterTelemetryRowTest, HeaderMatchesCanonicalSchema) {
    // The header is the single source of truth; the row must align
    // column-for-column with it.
    const std::string header = vehicle_sim::telemetry::csvHeaderLine();
    const auto names = vehicle_sim::test::splitFields(header);
    EXPECT_EQ(names.size(), vehicle_sim::test::CSV_FIELD_COUNT);
    EXPECT_EQ(names[0], "timestamp_ms");
    EXPECT_EQ(names[12], "dbc_signal_count");
    EXPECT_NE(std::find(names.begin(), names.end(), "brake_light"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "brake_percent"), names.end());
}

TEST(CsvRowFormatterTelemetryRowTest, RowHasThirteenFields) {
    const auto row = render(sampleRow());
    const auto fields = vehicle_sim::test::splitFields(row);
    EXPECT_EQ(fields.size(), vehicle_sim::test::CSV_FIELD_COUNT);
}

// ================================================
// Values by column
// ================================================

TEST(CsvRowFormatterTelemetryRowTest, RowValuesByColumn) {
    const auto row = render(sampleRow());
    const auto header = vehicle_sim::telemetry::csvHeaderLine();
    const auto cells = vehicle_sim::test::cellsByColumn(header, row);

    EXPECT_EQ(cells.at("timestamp_ms"), "1000");
    EXPECT_EQ(cells.at("vehicle_id"), "tesla");
    EXPECT_EQ(cells.at("speed_kmh"), "50.00");
    EXPECT_EQ(cells.at("throttle_percent"), "30.00");
    EXPECT_EQ(cells.at("brake_light"), "1");
    EXPECT_EQ(cells.at("acceleration_g"), "0.15");
    EXPECT_EQ(cells.at("steering_angle_deg"), "2.50");
    EXPECT_EQ(cells.at("motor_rpm"), "3000.00");
    EXPECT_EQ(cells.at("motor_hv_voltage"), "400.00");
    EXPECT_EQ(cells.at("motor_hv_current"), "60.00");
    EXPECT_EQ(cells.at("motor_torque_nm"), "450.00");
    EXPECT_EQ(cells.at("gear_selector"), "D");
    EXPECT_EQ(cells.at("dbc_signal_count"), "42");
}

// brake_light is a BINARY column: no 2-decimal formatting, tri-state rendering.
TEST(CsvRowFormatterTelemetryRowTest, BrakeLightFalseRendersZero) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.brake_light = 0;
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), render(r));
    EXPECT_EQ(cells.at("brake_light"), "0");
}

TEST(CsvRowFormatterTelemetryRowTest, BrakeLightAbsentRendersBlank) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.brake_light = std::nullopt;
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), render(r));
    EXPECT_EQ(cells.at("brake_light"), "");  // nullopt renders empty, not "0.00"
}

TEST(CsvRowFormatterTelemetryRowTest, DoublesRenderWithTwoDecimals) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.speed_kmh = 7.0;   // integer-valued double must still show two decimals
    const auto row = render(r);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), row);
    EXPECT_EQ(cells.at("speed_kmh"), "7.00");
}

// ================================================
// Byte-identical contract (the pinned strings)
// ================================================

// Byte-identical regression guard. The CSV DATA sink must render the canonical
// 13-column row with the exact bytes a downstream latency harness expects. Any
// "fix" that substitutes/escapes fields in the sink (e.g. wrapping with forLog)
// would break this exact-match assertion — that is the intended tripwire.
TEST(CsvRowFormatterTelemetryRowTest, RowIsByteIdenticalToExpected) {
    const auto row = render(sampleRow());
    EXPECT_EQ(row,
              "1000,tesla,50.00,30.00,1,0.15,2.50,3000.00,400.00,60.00,"
              "450.00,D,42");
}

// gear_selector is emitted RAW by the sink — a numeric label passes through
// unchanged. This pins the contract that the sink does NOT map 4097 -> "D";
// that mapping is the producer's responsibility.
TEST(CsvRowFormatterTelemetryRowTest, NumericGearSelectorPassesThroughUnchanged) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.gear_selector = vehicle_sim::telemetry::GearSelector::fromUserInput("4097");
    const auto row = render(r);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), row);
    EXPECT_EQ(cells.at("gear_selector"), "4097");
}

// An absent gear_selector renders an empty cell — "not reported", distinct from a
// definite label. Pins the tri-state contract for the string column.
TEST(CsvRowFormatterTelemetryRowTest, AbsentGearSelectorRendersBlank) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.gear_selector = vehicle_sim::telemetry::GearSelector{};  // empty
    const auto row = render(r);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), row);
    EXPECT_EQ(cells.at("gear_selector"), "");
}

// A control character in the gear label is sanitized to '?' at the input
// boundary (GearSelector::fromUserInput -> forLog), so the sink never sees a
// control byte. The CSV contract is preserved for valid input and defended for
// crafted input. Pins the sanitization byte-for-byte.
TEST(CsvRowFormatterTelemetryRowTest, ControlCharacterInGearSelectorSanitized) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    std::string crafted = "a";
    crafted += '\x07';  // BEL control byte
    crafted += "b";
    r.gear_selector = vehicle_sim::telemetry::GearSelector::fromUserInput(crafted);
    const auto row = render(r);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), row);
    EXPECT_EQ(cells.at("gear_selector"), "a?b");
}

// ================================================
// dbc_signal_count (countPopulated) — schema-owned
// ================================================

// dbc_signal_count counts brakeLight among the 10 translated columns.
TEST(CsvRowFormatterVehicleSignalTest, DbcSignalCountCountsBrakeLight) {
    const vehicle_sim::domain::VehicleSignal signal(
        vehicle_sim::domain::VehicleSignal::Params{
            .timestampUtcMs = 1ULL, .brakeLight = true});

    // brake_light is a binary column: optional<bool> -> optional<int> (1/0/blank).
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

// ================================================
// GearSelector validated type
// ================================================

TEST(GearSelectorTest, FromUserInputPassesThroughPrintable) {
    const auto g = vehicle_sim::telemetry::GearSelector::fromUserInput("D");
    EXPECT_EQ(g.asString(), "D");
    EXPECT_FALSE(g.empty());
}

TEST(GearSelectorTest, FromRegistryPassesThroughPrintable) {
    const auto g = vehicle_sim::telemetry::GearSelector::fromRegistry("4097");
    EXPECT_EQ(g.asString(), "4097");
}

TEST(GearSelectorTest, DefaultConstructsEmpty) {
    const vehicle_sim::telemetry::GearSelector g;
    EXPECT_TRUE(g.empty());
    EXPECT_EQ(g.asString(), "");
}

TEST(GearSelectorTest, SanitizesControlBytesToQuestionMark) {
    std::string raw = "R";
    raw += '\x1F';  // control byte just below 0x20
    const auto g = vehicle_sim::telemetry::GearSelector::fromUserInput(raw);
    EXPECT_EQ(g.asString(), "R?");
}

TEST(GearSelectorTest, SanitizesDelByte) {
    std::string raw = "P";
    raw += '\x7F';  // DEL
    const auto g = vehicle_sim::telemetry::GearSelector::fromUserInput(raw);
    EXPECT_EQ(g.asString(), "P?");
}

TEST(GearSelectorTest, SparesPrintablePunctuation) {
    // forLog must NOT alter printable punctuation (a gear label could carry it);
    // only control bytes (< 0x20 or 0x7F) become '?'.
    const auto g = vehicle_sim::telemetry::GearSelector::fromUserInput("D-1_2");
    EXPECT_EQ(g.asString(), "D-1_2");
}

TEST(GearSelectorTest, ComparesEqualToStringView) {
    const auto g = vehicle_sim::telemetry::GearSelector::fromUserInput("N");
    EXPECT_EQ(g, "N");
    EXPECT_EQ("N", g);
}

TEST(GearSelectorTest, ComparesEqualsOtherGearSelector) {
    const auto a = vehicle_sim::telemetry::GearSelector::fromUserInput("D");
    const auto b = vehicle_sim::telemetry::GearSelector::fromRegistry("D");
    EXPECT_EQ(a, b);
}

// ================================================
// VehicleId validated type (dedicated unit test)
// ================================================

TEST(VehicleIdTest, FromUserInputPassesThroughPrintable) {
    const auto v = vehicle_sim::telemetry::VehicleId::fromUserInput("tesla");
    EXPECT_EQ(v.asString(), "tesla");
    EXPECT_FALSE(v.empty());
}

TEST(VehicleIdTest, DefaultConstructsEmpty) {
    const vehicle_sim::telemetry::VehicleId v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.asString(), "");
}

TEST(VehicleIdTest, SanitizesControlBytesToQuestionMark) {
    std::string raw = "car";
    raw += '\x00';
    raw += "id";
    const auto v = vehicle_sim::telemetry::VehicleId::fromUserInput(raw);
    EXPECT_EQ(v.asString(), "car?id");
}

TEST(VehicleIdTest, ComparesEqualToStringView) {
    const auto v = vehicle_sim::telemetry::VehicleId::fromUserInput("ford");
    EXPECT_EQ(v, "ford");
    EXPECT_EQ("ford", v);
}

// ================================================
// csvNumericCell — structural guarantee for numeric CSV cells
// ================================================

// The values this pipeline emits (integers, 2-decimal fixed doubles, exponents)
// are a strict subset of the numeric alphabet, so the function is a NO-OP on
// them. This pins the byte-identical CSV contract: a real telemetry value must
// render unchanged.
TEST(CsvCellTest, IsNoOpOnSchemaValues) {
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("1000"), "1000");
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("50.00"), "50.00");
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("2.50"), "2.50");
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("-12.34"), "-12.34");
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("3.40e+02"), "3.40e+02");
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("42"), "42");
}

// A numeric cell must never smuggle a field delimiter or a record terminator:
// either would shift every later column or split one record into two. The
// whitelist DROPS those bytes, which is the corruption the contract prohibits.
TEST(CsvCellTest, DropsDelimiterAndLineTerminators) {
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("1,000.00"), "1000.00");  // comma removed
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("12\r34"), "1234");       // CR removed
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("56\n78"), "5678");       // LF removed
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("9\t0"), "90");           // tab removed
    EXPECT_EQ(vehicle_sim::telemetry::csvNumericCell("a1b2c"), "12");          // non-numeric letters removed
}

// formatOptional renders exactly two decimals via integer scaling (no streaming
// the external double into a sink), and the text is built from integers so it
// survives the numeric-cell whitelist byte-for-byte. Exercises the real path
// through csvRowLine (the public sink), so a regression (locale comma, lost
// precision) is caught.
TEST(CsvCellTest, FormatOptionalTwoDecimalsViaIntegerScaling) {
    const auto row = vehicle_sim::telemetry::csvRowLine(
        vehicle_sim::telemetry::CsvRowParams{
            1000ULL,
            vehicle_sim::telemetry::VehicleId::fromUserInput("tesla"),
            std::optional<double>{7.0},    // speed_kmh
            std::optional<double>{2.5},    // throttle_percent
            std::nullopt,
            std::optional<double>{-12.34}, // acceleration_g
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            vehicle_sim::telemetry::GearSelector::fromRegistry("D"),
            0,
        });
    // speed_kmh=7.00, throttle_percent=2.50, acceleration_g=-12.34
    EXPECT_NE(row.find("1000,tesla,7.00,2.50,,"), std::string::npos);
    EXPECT_NE(row.find(",-12.34,"), std::string::npos);
}

