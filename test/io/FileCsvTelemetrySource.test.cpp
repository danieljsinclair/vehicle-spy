#include "vehicle-sim/io/FileCsvTelemetrySource.h"

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

} // namespace

TEST(FileCsvTelemetrySourceTest, ParsesCanonicalSchema) {
    TmpCsv f(
        "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_percent,"
        "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
        "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count\n"
        "1000,tesla,50.0,30.0,0.0,0.15,2.5,3000.0,400.0,60.0,450.0,D,42\n");
    vehicle_sim::io::FileCsvTelemetrySource src(f.path());

    ASSERT_TRUE(src.hasNext());
    auto row = src.next();
    EXPECT_EQ(row.timestamp_ms, 1000u);
    EXPECT_EQ(row.vehicle_id, "tesla");
    EXPECT_DOUBLE_EQ(row.speed_kmh, 50.0);
    EXPECT_DOUBLE_EQ(row.throttle_percent, 30.0);
    EXPECT_DOUBLE_EQ(row.steering_angle_deg, 2.5);
    EXPECT_EQ(row.gear_selector, "D");
    EXPECT_EQ(row.dbc_signal_count, 42);
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
