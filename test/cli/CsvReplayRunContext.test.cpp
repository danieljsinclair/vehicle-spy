#include "vehicle-sim/cli/CsvReplayRunContext.h"
#include "vehicle-sim/io/CsvTelemetrySource.h"
#include "vehicle-sim/util/IClock.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include "telemetry/CsvShape.h"

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

namespace {

// In-memory source for deterministic replay tests (no file system).
class MemSource : public vehicle_sim::io::CsvTelemetrySource {
public:
    explicit MemSource(std::vector<vehicle_sim::telemetry::CsvTelemetryRow> rows)
        : rows_{std::move(rows)} {}
    bool hasNext() const override { return idx_ < rows_.size(); }
    vehicle_sim::telemetry::CsvTelemetryRow next() override { return rows_[idx_++]; }
    std::string name() const override { return "mem"; }
private:
    std::vector<vehicle_sim::telemetry::CsvTelemetryRow> rows_;
    size_t idx_{0};
};

// Instant clock: sleepFor does nothing, so replay is wall-clock-free.
class InstantClock : public vehicle_sim::util::IClock {
public:
    [[nodiscard]] time_point now() const override { return time_point{}; }
    void sleepFor(std::chrono::milliseconds) override {}
protected:
    [[nodiscard]] bool waitForImpl(std::condition_variable&,
                                   std::unique_lock<std::mutex>&,
                                   const std::function<bool()>&,
                                   time_point) const override { return false; }
};

// Clock that records every sleepFor duration instead of sleeping, so a test
// can assert WHAT the replay loop asked to sleep, not just that it finished.
class RecordingClock : public vehicle_sim::util::IClock {
public:
    [[nodiscard]] time_point now() const override { return time_point{}; }
    void sleepFor(std::chrono::milliseconds d) override {
        sleeps.push_back(d.count());
    }
    [[nodiscard]] bool waitForImpl(std::condition_variable&,
                                   std::unique_lock<std::mutex>&,
                                   const std::function<bool()>&,
                                   time_point) const override { return false; }
    std::vector<long long> sleeps;
};

vehicle_sim::telemetry::CsvTelemetryRow row(std::uint64_t ts, double throttle) {
    vehicle_sim::telemetry::CsvTelemetryRow r;
    r.timestamp_ms = ts;
    r.throttle_percent = throttle;
    return r;
}

} // namespace

TEST(CsvReplayRunContextTest, EmitsHeaderThenRows) {
    InstantClock clock;
    std::vector<vehicle_sim::telemetry::CsvTelemetryRow> rows{row(1000, 10.0), row(2000, 20.0)};
    auto src = std::make_unique<MemSource>(rows);

    std::ostringstream out;
    int rc = vehicle_sim::cli::CsvReplayRunContext::run(
        std::move(src), "tesla", /*intervalMs=*/0, out, clock, /*stdoutCsv=*/true);

    EXPECT_EQ(rc, 0);
    auto lines = vehicle_sim::test::splitLines(out.str());
    // Exactly header + 2 data rows.
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], vehicle_sim::telemetry::csvHeaderLine());
    // Each data row has the 13-column schema.
    for (size_t i = 1; i < lines.size(); ++i) {
        EXPECT_EQ(vehicle_sim::test::splitFields(lines[i]).size(),
                  vehicle_sim::test::CSV_FIELD_COUNT);
    }
}

TEST(CsvReplayRunContextTest, VehicleIdOverrideWhenRowBlank) {
    InstantClock clock;
    auto rows = std::vector<vehicle_sim::telemetry::CsvTelemetryRow>{row(1000, 10.0)};
    rows[0].vehicle_id = vehicle_sim::telemetry::VehicleId::fromUserInput("");   // blank in the row
    auto src = std::make_unique<MemSource>(rows);

    std::ostringstream out;
    vehicle_sim::cli::CsvReplayRunContext::run(
        std::move(src), "ford", /*intervalMs=*/0, out, clock, true);

    // The vehicle_id cell (2nd column, after timestamp_ms) of the first data
    // row must be "ford" because the row's own id was blank.
    std::string body = out.str().substr(out.str().find('\n') + 1);  // after header
    std::string firstRow = body.substr(0, body.find('\n'));
    // Split on comma: [0]=timestamp_ms, [1]=vehicle_id.
    size_t c1 = firstRow.find(',');
    ASSERT_NE(c1, std::string::npos);
    std::string vehicleIdCell = firstRow.substr(c1 + 1, firstRow.find(',', c1 + 1) - (c1 + 1));
    EXPECT_EQ(vehicleIdCell, "ford");
}

TEST(CsvReplayRunContextTest, EmptySourceReturnsFailure) {
    InstantClock clock;
    auto src = std::make_unique<MemSource>(std::vector<vehicle_sim::telemetry::CsvTelemetryRow>{});
    std::ostringstream out;
    int rc = vehicle_sim::cli::CsvReplayRunContext::run(
        std::move(src), "tesla", 0, out, clock, true);
    EXPECT_EQ(rc, 1);
}

TEST(CsvReplayRunContextTest, TimestampDrivenPacingDoesNotThrow) {
    // With an instant clock, timestamp-driven pacing is observable as a clean
    // run: the context must not block on wall-clock sleeps.
    InstantClock clock;
    std::vector<vehicle_sim::telemetry::CsvTelemetryRow> rows{
        row(1000, 10.0), row(1500, 20.0), row(2500, 30.0)};
    auto src = std::make_unique<MemSource>(rows);

    std::ostringstream out;
    int rc = vehicle_sim::cli::CsvReplayRunContext::run(
        std::move(src), "tesla", /*intervalMs=*/0, out, clock, true);
    EXPECT_EQ(rc, 0);
    // 3 data rows => header + 3 rows = 4 newline-terminated lines.
    EXPECT_EQ(vehicle_sim::test::splitLines(out.str()).size(), 4u);
}

// intervalMs=0 is the default replay pacing: sleep exactly the recorded
// timestamp delta between consecutive rows — never a fixed interval. This is
// the contract main.cpp relies on when the operator did not pass -i/--interval
// (the 500ms live default must not slow replay to 2 rows/sec).
TEST(CsvReplayRunContextTest, TimestampDrivenPacingSleepsRecordedDeltas) {
    RecordingClock clock;
    // Recorded deltas: 1000->1500 = 500ms, 1500->1500 = 0ms, 1500->1750 = 250ms.
    std::vector<vehicle_sim::telemetry::CsvTelemetryRow> rows{
        row(1000, 10.0), row(1500, 20.0), row(1500, 25.0), row(1750, 30.0)};
    auto src = std::make_unique<MemSource>(rows);

    std::ostringstream out;
    vehicle_sim::cli::CsvReplayRunContext::run(
        std::move(src), "tesla", /*intervalMs=*/0, out, clock, true);

    // No sleep before the first row; then one sleep per recorded delta.
    EXPECT_EQ(clock.sleeps, (std::vector<long long>{500, 0, 250}));
}

// intervalMs>0 is the explicit override: fixed sleeps regardless of the
// recorded deltas.
TEST(CsvReplayRunContextTest, ExplicitIntervalPacesFixedRegardlessOfTimestamps) {
    RecordingClock clock;
    std::vector<vehicle_sim::telemetry::CsvTelemetryRow> rows{
        row(1000, 10.0), row(2000, 20.0), row(2010, 30.0)};
    auto src = std::make_unique<MemSource>(rows);

    std::ostringstream out;
    vehicle_sim::cli::CsvReplayRunContext::run(
        std::move(src), "tesla", /*intervalMs=*/250, out, clock, true);

    EXPECT_EQ(clock.sleeps, (std::vector<long long>{250, 250}));
}
