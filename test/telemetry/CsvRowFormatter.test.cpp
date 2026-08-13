// CsvRowFormatter.test.cpp - CsvTelemetryRow overload renders the canonical
// --stdout-csv schema (byte-identical shape to the VehicleSignal overload).
//
// Business value: replay and interactive modes must produce rows a downstream
// --live-telemetry latency harness cannot distinguish from live output. This
// pins the overload to the same 13-column schema and 2-decimal precision.

#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include "telemetry/CsvShape.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

namespace {

vehicle_sim::telemetry::CsvTelemetryRow sampleRow() {
    vehicle_sim::telemetry::CsvTelemetryRow r;
    r.timestamp_ms       = 1000;
    r.vehicle_id         = "tesla";
    r.speed_kmh          = 50.0;
    r.throttle_percent   = 30.0;
    r.brake_light        = 1;
    r.acceleration_g     = 0.15;
    r.steering_angle_deg = 2.5;
    r.motor_rpm          = 3000.0;
    r.motor_hv_voltage   = 400.0;
    r.motor_hv_current   = 60.0;
    r.motor_torque_nm    = 450.0;
    r.gear_selector      = "D";
    r.dbc_signal_count   = 42;
    return r;
}

} // namespace

TEST(CsvRowFormatterTelemetryRowTest, HeaderMatchesCanonicalSchema) {
    // The header is the single source of truth; the row overload must align
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
    const auto row = vehicle_sim::telemetry::csvRowLine(sampleRow());
    const auto fields = vehicle_sim::test::splitFields(row);
    EXPECT_EQ(fields.size(), vehicle_sim::test::CSV_FIELD_COUNT);
}

TEST(CsvRowFormatterTelemetryRowTest, RowValuesByColumn) {
    const auto row = vehicle_sim::telemetry::csvRowLine(sampleRow());
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
        vehicle_sim::telemetry::csvHeaderLine(), vehicle_sim::telemetry::csvRowLine(r));
    EXPECT_EQ(cells.at("brake_light"), "0");
}

TEST(CsvRowFormatterTelemetryRowTest, BrakeLightAbsentRendersBlank) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.brake_light = std::nullopt;
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), vehicle_sim::telemetry::csvRowLine(r));
    EXPECT_EQ(cells.at("brake_light"), "");  // nullopt renders empty, not "0.00"
}

TEST(CsvRowFormatterTelemetryRowTest, DoublesRenderWithTwoDecimals) {
    vehicle_sim::telemetry::CsvTelemetryRow r = sampleRow();
    r.speed_kmh = 7.0;   // integer-valued double must still show two decimals
    const auto row = vehicle_sim::telemetry::csvRowLine(r);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), row);
    EXPECT_EQ(cells.at("speed_kmh"), "7.00");
}

// dbc_signal_count counts brakeLight among the 10 translated columns.
TEST(CsvRowFormatterVehicleSignalTest, DbcSignalCountCountsBrakeLight) {
    const vehicle_sim::domain::VehicleSignal signal(
        vehicle_sim::domain::VehicleSignal::Params{
            .timestampUtcMs = 1ULL, .brakeLight = true});
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(),
        vehicle_sim::telemetry::csvRowLine(signal, ""));
    EXPECT_EQ(cells.at("brake_light"), "1");
    EXPECT_EQ(cells.at("dbc_signal_count"), "1");
}
