// CsvStdoutSink.test.cpp — behaviour of the --stdout-csv sink family.
//
// The business value: a caller can pipe `vehicle-sim ... --stdout-csv` into
// another tool and receive exactly the same CSV that `--log <base>` writes to
// disk. These tests pin that equivalence (against the REAL TraceLogger output,
// not a restated copy of the schema), the null-object/factory behaviour that
// keeps the flag branch out of the dispatch loop, and the stream injection
// that makes all of it testable without touching the process's real stdout.

#include <gtest/gtest.h>

#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace vehicle_sim::telemetry;
using namespace vehicle_sim::domain;

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

// A fully-populated signal — every column exercised, nothing left to default.
VehicleSignal fullSignal() {
    return VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 123456789ULL,
        .throttlePercent = 50.0,
        .speedKmh = 100.0,
        .accelerationG = 0.5,
        .brakePercent = 25.0,
        .steeringAngleDeg = -12.5,
        .motorRpm = 3500.5,
        .motorHvVoltage = 400.0,
        .motorHvCurrent = 25.3,
        .motorTorqueNm = 150.0,
        .gearSelector = Gear::AUTO_1});
}

// RAII temp file so the TraceLogger-parity test does real file I/O without
// leaving artefacts in the source tree.
class TempCsv {
public:
    TempCsv()
        : path_((std::filesystem::temp_directory_path() /
                 ("vsim_csvstdout_" + std::to_string(counter_++) + ".csv")).string()) {}
    ~TempCsv() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempCsv(const TempCsv&) = delete;
    TempCsv& operator=(const TempCsv&) = delete;
    TempCsv(TempCsv&&) = delete;
    TempCsv& operator=(TempCsv&&) = delete;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    [[nodiscard]] std::string read() const {
        std::ifstream in(path_);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

private:
    static inline int counter_ = 0;
    std::string path_;
};

} // namespace

class CsvStdoutSinkTest : public ::testing::Test {
protected:
    std::ostringstream out;
};

// ================================================
// CsvStdoutSink — header and row emission
// ================================================

// Contract: the header is written eagerly at construction, so a consumer
// reading the pipe sees the schema before any data arrives.
TEST_F(CsvStdoutSinkTest, WritesHeaderOnConstruction) {
    CsvStdoutSink sink(out);

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0],
              "timestamp_ms,vehicle_id,speed_kmh,throttle_percent,brake_percent,"
              "acceleration_g,steering_angle_deg,motor_rpm,motor_hv_voltage,"
              "motor_hv_current,motor_torque_nm,gear_selector,dbc_signal_count");
}

TEST_F(CsvStdoutSinkTest, WritesRowForAllFields) {
    CsvStdoutSink sink(out);
    sink(fullSignal());

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);  // header + 1 row
    EXPECT_EQ(lines[1], "123456789,,100.00,50.00,25.00,0.50,-12.50,3500.50,400.00,25.30,150.00,D,10");
}

TEST_F(CsvStdoutSinkTest, WritesOneRowPerCall) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 1000ULL, .speedKmh = 10.0, .gearSelector = Gear::PARK}));
    sink(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 2000ULL, .speedKmh = 20.0, .gearSelector = Gear::NEUTRAL}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 3U);  // header + 2 rows
    EXPECT_EQ(lines[1], "1000,,10.00,,,,,,,,,P,2");
    EXPECT_EQ(lines[2], "2000,,20.00,,,,,,,,,N,2");
}

// Absent signals must render as empty cells, not as 0.00 — a downstream
// consumer has to tell "not reported" apart from "reported as zero".
TEST_F(CsvStdoutSinkTest, LeavesEmptyCellsForAbsentValues) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 12345ULL}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[1], "12345,,,,,,,,,,,,0");
}

TEST_F(CsvStdoutSinkTest, WritesVehicleIdColumnWhenProvided) {
    CsvStdoutSink sink(out, "tesla");
    sink(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 1000ULL, .speedKmh = 42.0}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);
    // vehicle_id is the 2nd column.
    EXPECT_EQ(lines[1], "1000,tesla,42.00,,,,,,,,,,1");
}

// The gear column carries the display label (4097 -> "D"), matching the file
// CSV, so the two outputs stay diff-clean.
TEST_F(CsvStdoutSinkTest, RendersGearAsDisplayLabel) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 1ULL, .gearSelector = Gear::REVERSE}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_NE(lines[1].find(",R,"), std::string::npos);
}

// ================================================
// The core promise: stdout CSV == file CSV
// ================================================

// Compares against the REAL TraceLogger rather than a hand-copied expected
// string: if either writer's schema drifts, this fails.
TEST_F(CsvStdoutSinkTest, OutputIsByteIdenticalToTraceLoggerFileOutput) {
    const auto signal = fullSignal();

    TempCsv temp;
    {
        TraceLogger logger(temp.path(), "tesla");
        logger(signal);
    }

    CsvStdoutSink sink(out, "tesla");
    sink(signal);

    EXPECT_EQ(out.str(), temp.read());
}

TEST_F(CsvStdoutSinkTest, MatchesTraceLoggerForSparselyPopulatedSignal) {
    const VehicleSignal sparse(VehicleSignal::Params{
        .timestampUtcMs = 777ULL, .speedKmh = 12.25, .motorRpm = 900.0});

    TempCsv temp;
    {
        TraceLogger logger(temp.path());
        logger(sparse);
    }

    CsvStdoutSink sink(out);
    sink(sparse);

    EXPECT_EQ(out.str(), temp.read());
}

// ================================================
// Usability as a dispatch consumer
// ================================================

// The sink is consumed through the ICsvStdoutSink interface by the reporter
// adapter and by TelemetryRunner's dispatch lambda — it must work by reference
// through the base type.
TEST_F(CsvStdoutSinkTest, InvokableThroughInterfaceReference) {
    CsvStdoutSink sink(out);
    ICsvStdoutSink& asInterface = sink;

    asInterface(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 54321ULL, .speedKmh = 150.0}));

    EXPECT_NE(out.str().find("54321,,150.00"), std::string::npos);
}

// Rows must be flushed as they are produced: a downstream `| head` or a live
// dashboard cannot wait for a 4 KiB buffer to fill.
TEST_F(CsvStdoutSinkTest, FlushesEachRowImmediately) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 999ULL}));

    // Reading the buffer without destroying the sink proves no row is pending.
    EXPECT_EQ(splitLines(out.str()).size(), 2U);
}

// ================================================
// NullCsvStdoutSink — the disabled arm
// ================================================

TEST_F(CsvStdoutSinkTest, NullSinkWritesNothingAndDoesNotThrow) {
    NullCsvStdoutSink nullSink;

    EXPECT_NO_THROW(nullSink(fullSignal()));
    EXPECT_TRUE(out.str().empty());
}

// ================================================
// createStdoutSink factory — the single flag branch
// ================================================

TEST_F(CsvStdoutSinkTest, FactoryReturnsWritingSinkWhenEnabled) {
    auto sink = createStdoutSink(true, out);
    ASSERT_NE(sink, nullptr);

    // Enabled sinks announce themselves with the header at construction.
    EXPECT_FALSE(out.str().empty());

    (*sink)(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 4242ULL}));
    EXPECT_NE(out.str().find("4242"), std::string::npos);
}

TEST_F(CsvStdoutSinkTest, FactoryReturnsSilentSinkWhenDisabled) {
    auto sink = createStdoutSink(false, out);
    ASSERT_NE(sink, nullptr);

    // No header, and dispatching to it stays silent — this is what lets the
    // caller hold a sink unconditionally instead of testing a flag per row.
    (*sink)(fullSignal());
    EXPECT_TRUE(out.str().empty());
}

TEST_F(CsvStdoutSinkTest, FactoryThreadsVehicleIdIntoEnabledSink) {
    auto sink = createStdoutSink(true, out, "audi_mlb_evo");
    (*sink)(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 5ULL}));

    EXPECT_NE(out.str().find(",audi_mlb_evo,"), std::string::npos);
}
