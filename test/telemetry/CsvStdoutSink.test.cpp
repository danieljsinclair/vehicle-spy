// CsvStdoutSink.test.cpp — behaviour of the --stdout-csv sink family.
//
// The business value: a caller can pipe `vehicle-sim ... --stdout-csv` into
// another tool and receive exactly the same CSV that `--log <base>` writes to
// disk. These tests pin that equivalence (against the REAL TraceLogger output,
// not a restated copy of the schema), the null-object/factory behaviour that
// keeps the flag branch out of the dispatch loop, and the stream injection
// that makes all of it testable without touching the process's real stdout.

#include <gtest/gtest.h>

#include "CsvShape.h"

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
using vehicle_sim::test::cellsByColumn;
using vehicle_sim::test::CSV_FIELD_COUNT;
using vehicle_sim::test::splitFields;
using vehicle_sim::test::splitLines;

namespace {

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
//
// Asserted as shape + key column names, not as one literal: a downstream
// parser keys off the names it needs, so adding a column must not fail this.
// The exact bytes stay pinned by the TraceLogger byte-identity tests below.
TEST_F(CsvStdoutSinkTest, WritesHeaderOnConstruction) {
    CsvStdoutSink sink(out);

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(splitFields(lines[0]).size(), CSV_FIELD_COUNT);

    // The columns a consumer actually addresses: the time base it joins on,
    // the gear label, and the populated-signal count it filters by.
    EXPECT_NE(lines[0].find("timestamp_ms"), std::string::npos);
    EXPECT_NE(lines[0].find("gear_selector"), std::string::npos);
    EXPECT_NE(lines[0].find("dbc_signal_count"), std::string::npos);
}

TEST_F(CsvStdoutSinkTest, WritesRowForAllFields) {
    CsvStdoutSink sink(out);
    sink(fullSignal());

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);  // header + 1 row
    EXPECT_EQ(splitFields(lines[1]).size(), CSV_FIELD_COUNT);

    // Values are checked by column NAME: a fully-populated signal renders every
    // value it carries, and dbc_signal_count reports all 10 translated signals.
    const auto cells = cellsByColumn(lines[0], lines[1]);
    EXPECT_EQ(cells.at("timestamp_ms"), "123456789");
    EXPECT_EQ(cells.at("speed_kmh"), "100.00");
    EXPECT_EQ(cells.at("motor_rpm"), "3500.50");
    EXPECT_EQ(cells.at("steering_angle_deg"), "-12.50");
    EXPECT_EQ(cells.at("gear_selector"), "D");
    EXPECT_EQ(cells.at("dbc_signal_count"), "10");
}

TEST_F(CsvStdoutSinkTest, WritesOneRowPerCall) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 1000ULL, .speedKmh = 10.0, .gearSelector = Gear::PARK}));
    sink(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 2000ULL, .speedKmh = 20.0, .gearSelector = Gear::NEUTRAL}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 3U);  // header + 2 rows

    // One record per call, each well-formed, each carrying ITS OWN signal's
    // values — the property that makes the stream a faithful row-per-frame log.
    EXPECT_EQ(splitFields(lines[1]).size(), CSV_FIELD_COUNT);
    EXPECT_EQ(splitFields(lines[2]).size(), CSV_FIELD_COUNT);

    const auto first = cellsByColumn(lines[0], lines[1]);
    EXPECT_EQ(first.at("timestamp_ms"), "1000");
    EXPECT_EQ(first.at("speed_kmh"), "10.00");
    EXPECT_EQ(first.at("gear_selector"), "P");
    EXPECT_EQ(first.at("dbc_signal_count"), "2");

    const auto second = cellsByColumn(lines[0], lines[2]);
    EXPECT_EQ(second.at("timestamp_ms"), "2000");
    EXPECT_EQ(second.at("speed_kmh"), "20.00");
    EXPECT_EQ(second.at("gear_selector"), "N");
    EXPECT_EQ(second.at("dbc_signal_count"), "2");
}

// Absent signals must render as empty cells, not as 0.00 — a downstream
// consumer has to tell "not reported" apart from "reported as zero".
TEST_F(CsvStdoutSinkTest, LeavesEmptyCellsForAbsentValues) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 12345ULL}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);

    // Still a full-width record — absent values occupy their cells rather than
    // collapsing the row — and every signal cell is EMPTY, never "0.00".
    ASSERT_EQ(splitFields(lines[1]).size(), CSV_FIELD_COUNT);

    const auto cells = cellsByColumn(lines[0], lines[1]);
    EXPECT_EQ(cells.at("timestamp_ms"), "12345");
    EXPECT_TRUE(cells.at("speed_kmh").empty());
    EXPECT_TRUE(cells.at("throttle_percent").empty());
    EXPECT_TRUE(cells.at("motor_rpm").empty());
    EXPECT_TRUE(cells.at("gear_selector").empty());
    EXPECT_EQ(cells.at("dbc_signal_count"), "0");
}

TEST_F(CsvStdoutSinkTest, WritesVehicleIdColumnWhenProvided) {
    CsvStdoutSink sink(out, "tesla");
    sink(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 1000ULL, .speedKmh = 42.0}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(splitFields(lines[1]).size(), CSV_FIELD_COUNT);

    // The constructor's vehicleId lands in the vehicle_id column — the tag a
    // consumer uses to demultiplex rows from several vehicles on one pipe.
    const auto cells = cellsByColumn(lines[0], lines[1]);
    EXPECT_EQ(cells.at("vehicle_id"), "tesla");
    EXPECT_EQ(cells.at("timestamp_ms"), "1000");
    EXPECT_EQ(cells.at("speed_kmh"), "42.00");
    EXPECT_EQ(cells.at("dbc_signal_count"), "1");
}

// The gear column carries the display label (4097 -> "D"), matching the file
// CSV, so the two outputs stay diff-clean.
TEST_F(CsvStdoutSinkTest, RendersGearAsDisplayLabel) {
    CsvStdoutSink sink(out);
    sink(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 1ULL, .gearSelector = Gear::REVERSE}));

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);

    // Named-column lookup, so this asserts the GEAR column holds the label and
    // not merely that an "R" appears somewhere in the record.
    EXPECT_EQ(cellsByColumn(lines[0], lines[1]).at("gear_selector"), "R");
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
