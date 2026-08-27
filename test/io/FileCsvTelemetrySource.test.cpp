#include "vehicle-sim/io/FileCsvTelemetrySource.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include "telemetry/CsvShape.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

// Writes `content` to a temp file and returns its path. Throws on failure so
// the failure surfaces through the normal exception path (no ASSERT_* in a
// constructor, which is ill-formed in C++).
std::string writeTmpCsv(const std::string& content) {
    char tmpl[] = "/tmp/vsim_csv_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        throw std::runtime_error("mkstemp failed");
    }
    std::string path = tmpl;
    std::ofstream out(path);
    out << content;
    out.close();
    if (!out.good()) {
        throw std::runtime_error("failed to write temp CSV");
    }
    return path;
}

// Writes `content` to a temp file and keeps it alive for the duration of the
// test (unlinked in the destructor).
class TmpCsv {
public:
    explicit TmpCsv(const std::string& content)
        : path_{writeTmpCsv(content)} {}
    ~TmpCsv() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// Render a (possibly partially-populated) CsvTelemetryRow through the single-
// param params-struct sink. Plain doubles are wrapped as present optionals (0.0
// default -> "0.00", matching the old raw-<< rendering byte-for-byte). brake_light
// stays optional<int> (already tri-state).
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

} // namespace

TEST(FileCsvTelemetrySourceTest, ParsesCanonicalSchema) {
    TmpCsv f(
        "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_light,"
        "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
        "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count\n"
        "1000,tesla,50.0,30.0,1,0.15,2.5,3000.0,400.0,60.0,450.0,D,42\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());

    ASSERT_TRUE(src.hasNext());
    auto row = src.next();
    EXPECT_EQ(row.timestamp_ms, 1000u);
    EXPECT_EQ(row.vehicle_id, "tesla");
    EXPECT_DOUBLE_EQ(row.speed_kmh, 50.0);
    EXPECT_DOUBLE_EQ(row.throttle_percent, 30.0);
    ASSERT_TRUE(row.brake_light.has_value());
    EXPECT_EQ(*row.brake_light, 1);
    EXPECT_DOUBLE_EQ(row.steering_angle_deg, 2.5);
    EXPECT_EQ(row.gear_selector, "D");
    EXPECT_EQ(row.dbc_signal_count, 42);
}

// brake_light is tri-state: blank cell parses as absent, "0" as definite off.
// Round-trip proof: a line produced by csvRowLine survives the source intact.
TEST(FileCsvTelemetrySourceTest, BrakeLightBlankCellParsesAbsent) {
    {
        TmpCsv f("timestamp_ms,brake_light\n1000,\n");
        vehicle_sim::io::FileCsvTelemetrySource src(f.path());
        auto row = src.next();
        EXPECT_FALSE(row.brake_light.has_value());  // blank -> not reported
    }
    {
        TmpCsv f("timestamp_ms,brake_light\n1000,0\n");
        vehicle_sim::io::FileCsvTelemetrySource src(f.path());
        auto row = src.next();
        ASSERT_TRUE(row.brake_light.has_value());
        EXPECT_EQ(*row.brake_light, 0);  // definite off
    }
    {
        // Round trip: the formatter's own output reads back with the tri-state
        // preserved (emits, source reads back).
        vehicle_sim::telemetry::CsvTelemetryRow out;
        out.timestamp_ms = 1000;
        out.brake_light = std::nullopt;
        TmpCsv f(vehicle_sim::telemetry::csvHeaderLine() + "\n" +
                 renderRow(out) + "\n");
        vehicle_sim::io::FileCsvTelemetrySource src(f.path());
        auto row = src.next();
        EXPECT_FALSE(row.brake_light.has_value());

        out.brake_light = 1;
        TmpCsv g(vehicle_sim::telemetry::csvHeaderLine() + "\n" +
                 renderRow(out) + "\n");
        vehicle_sim::io::FileCsvTelemetrySource src2(g.path());
        row = src2.next();
        ASSERT_TRUE(row.brake_light.has_value());
        EXPECT_EQ(*row.brake_light, 1);
    }
}

// Old captures recorded with brake_percent (no brake_light column) stay
// replayable: the constructor does not throw, rows parse, brake_light absent.
TEST(FileCsvTelemetrySourceTest, OldSchemaWithBrakePercentStillLoads) {
    TmpCsv f(
        "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_percent,"
        "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
        "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count\n"
        "1000,tesla,50.0,30.0,25.0,0.15,2.5,3000.0,400.0,60.0,450.0,D,42\n");
    EXPECT_NO_THROW({
        vehicle_sim::io::FileCsvTelemetrySource src(f.path());
        EXPECT_TRUE(src.hasNext());
        auto row = src.next();
        EXPECT_DOUBLE_EQ(row.speed_kmh, 50.0);
        EXPECT_FALSE(row.brake_light.has_value());  // old column ignored
    });
}

TEST(FileCsvTelemetrySourceTest, ColumnOrderIsIrrelevant) {
    TmpCsv f(
        "throttle_percent,speed_kmh,timestamp_ms\n"
        "70.0,35.0,2000\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());

    auto row = src.next();
    EXPECT_DOUBLE_EQ(row.throttle_percent, 70.0);
    EXPECT_DOUBLE_EQ(row.speed_kmh, 35.0);
    EXPECT_EQ(row.timestamp_ms, 2000u);
}

TEST(FileCsvTelemetrySourceTest, MissingColumnsDefault) {
    TmpCsv f("timestamp_ms,vehicle_id\n500,leaf\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());

    auto row = src.next();
    EXPECT_EQ(row.timestamp_ms, 500u);
    EXPECT_EQ(row.vehicle_id, "leaf");
    EXPECT_DOUBLE_EQ(row.throttle_percent, 0.0);   // defaulted
    EXPECT_EQ(row.dbc_signal_count, 0);            // defaulted
}

TEST(FileCsvTelemetrySourceTest, BlankLinesAreSkipped) {
    TmpCsv f(
        "timestamp_ms,vehicle_id,speed_kmh\n"
        "100,a,10.0\n"
        "\n"            // blank -> skipped
        "200,b,20.0\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());

    auto r1 = src.next();
    EXPECT_EQ(r1.timestamp_ms, 100u);
    auto r2 = src.next();
    EXPECT_EQ(r2.timestamp_ms, 200u);
}

TEST(FileCsvTelemetrySourceTest, MissingFileThrows) {
    EXPECT_THROW(
        vehicle_sim::io::FileCsvTelemetrySource("/nonexistent/path.csv"),
        std::runtime_error);
}

// End-to-end sanitization: a control byte in the gear_selector cell of a CSV
// file must be replaced with '?' by the time the row is rendered. This closes
// the loop on the file-derived taint path (GearSelector::fromUserInput ->
// forLog at FileCsvTelemetrySource -> csvRowLine -> rendered output).
TEST(FileCsvTelemetrySourceTest, ControlCharInGearSelectorSanitizedOnRender) {
    TmpCsv f(
        "timestamp_ms,vehicle_id,gear_selector\n"
        "1000,tesla,a\x07" "b\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());
    ASSERT_TRUE(src.hasNext());
    auto row = src.next();
    EXPECT_EQ(row.gear_selector, "a?b");
    const auto rendered = renderRow(row);
    const auto cells = vehicle_sim::test::cellsByColumn(
        vehicle_sim::telemetry::csvHeaderLine(), rendered);
    EXPECT_EQ(cells.at("gear_selector"), "a?b");
}
