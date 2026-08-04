// TelemetryRunnerStdoutCsv.test.cpp — the --stdout-csv arm of the legacy
// telemetry loop.
//
// The business promise of --stdout-csv is that `vehicle-sim ... --stdout-csv |
// some-tool` works: the stdout stream is CSV and ONLY CSV. That is a property
// of the whole run, not of the sink alone — the runner also has to move its
// banners and its per-row human display off that stream. These tests drive the
// REAL TelemetryRunner::run() loop and inspect the injected stream, which is
// the only way to observe that promise without hijacking the process's stdout.
//
// Kept separate from TelemetryRunner.test.cpp (SRP): that file owns the
// runner's config/logging contract, this one owns the stdout-CSV contract.

#include <gtest/gtest.h>

#include "../telemetry/CsvShape.h"

#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;
using vehicle_sim::test::cellsByColumn;
using vehicle_sim::test::CSV_FIELD_COUNT;
using vehicle_sim::test::splitFields;
using vehicle_sim::test::splitLines;
using vehicle_sim::test::startsWith;

namespace {

/// What the source recorded during a run. Owned by the TEST, not by the source:
/// run() takes ownership of the ISignalSource and destroys it before returning,
/// so a pointer to the source is dangling by the time assertions execute. The
/// record outlives the run and is the only safe thing to assert against.
struct SourceActivity {
    int polls = 0;
    bool started = false;
    bool stopped = false;
};

/// A source that stops the run itself once it has been polled enough times, so
/// the loop terminates on OBSERVED PROGRESS rather than on a wall-clock guess.
/// A timing-based stop would make these tests flaky on a loaded machine.
class SelfStoppingSource final : public ISignalSource {
public:
    SelfStoppingSource(vehicle_sim::pipeline::StopToken& stop,
                       VehicleSignal signal,
                       int pollsBeforeStop,
                       SourceActivity& activity)
        : stop_(stop),
          signal_(std::move(signal)),
          pollsBeforeStop_(pollsBeforeStop),
          activity_(activity) {}

    [[nodiscard]] VehicleSignal latestSignal() const noexcept override {
        ++activity_.polls;
        if (activity_.polls >= pollsBeforeStop_) {
            stop_.requestStop();
        }
        return signal_;
    }

    void start() override { activity_.started = true; }
    void stop() override { activity_.stopped = true; }

private:
    vehicle_sim::pipeline::StopToken& stop_;
    VehicleSignal signal_;
    int pollsBeforeStop_;
    SourceActivity& activity_;
};

VehicleSignal drivingSignal() {
    return VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 1700000000000ULL,
        .throttlePercent = 30.0,
        .speedKmh = 72.5,
        .gearSelector = Gear::AUTO_1});
}

/// Human-readable fragments the runner emits on its display stream. If any of
/// these reach the CSV stream a downstream parser breaks, so the CSV tests
/// assert their ABSENCE and the display test asserts their presence.
const std::vector<std::string>& humanOutputMarkers() {
    static const std::vector<std::string> markers{
        "Starting", "Press Ctrl", "Telemetry", "Goodbye", "Throttle:", "Speed:"};
    return markers;
}

/// RAII stream-buffer redirect. Lets a test observe what the runner wrote to a
/// process-global stream (std::cout / std::cerr) and restores it unconditionally,
/// so one failing assertion cannot leave the whole suite's output redirected.
class StreamCapture {
public:
    explicit StreamCapture(std::ostream& target)
        : target_(target), original_(target.rdbuf(buffer_.rdbuf())) {}
    ~StreamCapture() { target_.rdbuf(original_); }

    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;
    StreamCapture(StreamCapture&&) = delete;
    StreamCapture& operator=(StreamCapture&&) = delete;

    [[nodiscard]] std::string text() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::ostream& target_;
    std::streambuf* original_;
};

} // namespace

class TelemetryRunnerStdoutCsvTest : public ::testing::Test {
protected:
    void SetUp() override {
        stop_.reset();
        config_ = std::make_unique<VehicleConfig>(
            "test.dbc", "test.dbc", "Test Vehicle",
            std::unordered_map<std::string, std::string>{});
    }

    void TearDown() override { stop_.requestStop(); }

    /// Build the run options for the --stdout-csv arm.
    [[nodiscard]] TelemetryRunOptions csvOptions(std::ostream* csvStream) const {
        return TelemetryRunOptions{.logCsvPath = "",
                                   .logRawPath = "",
                                   .pollIntervalMs = 1,
                                   .stdoutCsv = true,
                                   .stdoutCsvStream = csvStream};
    }

    /// Drive the real loop with --stdout-csv on, CSV routed to csvStream.
    /// The human display lands on stderr, captured so it never pollutes the
    /// test suite's own output.
    int runWithCsvStream(std::ostream* csvStream, int pollsBeforeStop = 2) {
        const StreamCapture displayCapture(std::cerr);
        auto source = std::make_unique<SelfStoppingSource>(
            stop_, drivingSignal(), pollsBeforeStop, activity_);
        return TelemetryRunner::run(std::move(source), config_.get(),
                                    csvOptions(csvStream), stop_);
    }

    vehicle_sim::pipeline::StopToken stop_;
    std::unique_ptr<VehicleConfig> config_;
    std::ostringstream csvOut_;
    /// Outlives every run() call, so lifecycle assertions stay valid after the
    /// runner has destroyed the source it owned.
    SourceActivity activity_;
};

// ================================================
// M1 — the pipeable-stream promise, end to end through run()
// ================================================

// Contract: with stdoutCsv enabled and a stream injected, that stream receives
// a pure CSV document — schema line first, every line a full-width record, and
// no human-readable text anywhere. This is the property `| some-tool` needs.
TEST_F(TelemetryRunnerStdoutCsvTest, StdoutCsvStreamCarriesPureCsv) {
    const int exitCode = runWithCsvStream(&csvOut_);
    ASSERT_EQ(exitCode, 0);

    const std::string csv = csvOut_.str();
    ASSERT_FALSE(csv.empty()) << "--stdout-csv must write to the injected stream";

    // 1. Opens with the schema line, so a parser can bind columns immediately.
    EXPECT_TRUE(startsWith(csv, "timestamp_ms"))
        << "CSV stream must begin with the header; got: " << csv.substr(0, 60);

    // 2. Every line is a well-formed record of the full width.
    const auto lines = splitLines(csv);
    ASSERT_GE(lines.size(), 2U) << "expected header + at least one dispatched row";
    for (const auto& line : lines) {
        EXPECT_EQ(splitFields(line).size(), CSV_FIELD_COUNT)
            << "every CSV line must have " << CSV_FIELD_COUNT
            << " fields; offending line: " << line;
    }

    // 3. No banner or display text leaked onto the data stream.
    for (const auto& marker : humanOutputMarkers()) {
        EXPECT_EQ(csv.find(marker), std::string::npos)
            << "human-readable text '" << marker << "' must not reach the CSV stream";
    }
}

// The other half of the same promise: the display is not suppressed, it MOVES.
// An operator running `--stdout-csv | tool` still watches the live telemetry.
TEST_F(TelemetryRunnerStdoutCsvTest, HumanDisplayMovesToStderrWhenCsvIsOnStdout) {
    std::string displayText;
    int exitCode = 1;
    {
        const StreamCapture displayCapture(std::cerr);
        auto source = std::make_unique<SelfStoppingSource>(stop_, drivingSignal(), 2, activity_);
        exitCode = TelemetryRunner::run(std::move(source), config_.get(),
                                        csvOptions(&csvOut_), stop_);
        displayText = displayCapture.text();
    }

    ASSERT_EQ(exitCode, 0);
    EXPECT_NE(displayText.find("Starting"), std::string::npos)
        << "the run banner must still be shown, on stderr";
    EXPECT_NE(displayText.find("Throttle:"), std::string::npos)
        << "the per-row display must still be shown, on stderr";
}

// The rows must carry the SOURCE's data — proving the sink was registered as a
// real dispatch consumer, not merely constructed.
TEST_F(TelemetryRunnerStdoutCsvTest, DispatchedRowsCarryTheSourceSignalValues) {
    ASSERT_EQ(runWithCsvStream(&csvOut_), 0);

    const auto lines = splitLines(csvOut_.str());
    ASSERT_GE(lines.size(), 2U);

    const auto cells = cellsByColumn(lines[0], lines[1]);
    EXPECT_EQ(cells.at("timestamp_ms"), "1700000000000");
    EXPECT_EQ(cells.at("speed_kmh"), "72.50");
    EXPECT_EQ(cells.at("throttle_percent"), "30.00");
    EXPECT_EQ(cells.at("gear_selector"), "D");
}

// One row per dispatch: the loop must not batch or coalesce frames, otherwise a
// consumer's row count no longer matches the frames the vehicle produced. Also
// pins the source lifecycle — run() starts and stops what it was given.
TEST_F(TelemetryRunnerStdoutCsvTest, EmitsOneCsvRowPerDispatchedSignal) {
    auto source = std::make_unique<SelfStoppingSource>(
        stop_, drivingSignal(), 3, activity_);

    int exitCode = 1;
    {
        const StreamCapture displayCapture(std::cerr);
        exitCode = TelemetryRunner::run(std::move(source), config_.get(),
                                        csvOptions(&csvOut_), stop_);
    }

    ASSERT_EQ(exitCode, 0);
    EXPECT_TRUE(activity_.started);
    EXPECT_TRUE(activity_.stopped);

    // header + exactly one row per poll the loop actually performed.
    const auto lines = splitLines(csvOut_.str());
    EXPECT_EQ(lines.size(), static_cast<std::size_t>(activity_.polls) + 1U);
}

// ================================================
// M4 — the parameter object's stdout-CSV fields are honoured
// ================================================

// stdoutCsv=true + an injected stream => that stream is the one written to, and
// the process's real stdout is left completely untouched. Without this, the
// injection point would be untested and a regression could silently write to
// std::cout while every other test still passed.
TEST_F(TelemetryRunnerStdoutCsvTest, HonoursInjectedStdoutCsvStreamAndLeavesCoutUntouched) {
    std::ostringstream injected;
    std::string coutText;
    int exitCode = 1;
    {
        const StreamCapture coutCapture(std::cout);
        const StreamCapture displayCapture(std::cerr);
        auto source = std::make_unique<SelfStoppingSource>(stop_, drivingSignal(), 2, activity_);
        exitCode = TelemetryRunner::run(std::move(source), config_.get(),
                                        csvOptions(&injected), stop_);
        coutText = coutCapture.text();
    }

    ASSERT_EQ(exitCode, 0);
    EXPECT_TRUE(startsWith(injected.str(), "timestamp_ms"))
        << "the injected stream must receive the CSV";
    EXPECT_TRUE(coutText.empty())
        << "nothing may reach std::cout when a stream was injected; got: " << coutText;
}

// A null stream is the PRODUCTION default and must mean std::cout — not a crash
// and not a silent drop. This is the documented "nullptr means std::cout"
// contract, and it is the path the real CLI takes.
TEST_F(TelemetryRunnerStdoutCsvTest, NullStdoutCsvStreamDefaultsToStdCout) {
    std::string coutText;
    int exitCode = 1;
    {
        const StreamCapture coutCapture(std::cout);
        const StreamCapture displayCapture(std::cerr);
        auto source = std::make_unique<SelfStoppingSource>(stop_, drivingSignal(), 2, activity_);
        exitCode = TelemetryRunner::run(std::move(source), config_.get(),
                                        csvOptions(nullptr), stop_);
        coutText = coutCapture.text();
    }

    ASSERT_EQ(exitCode, 0);
    EXPECT_TRUE(startsWith(coutText, "timestamp_ms"))
        << "a null stdoutCsvStream must fall back to std::cout";
    for (const auto& marker : humanOutputMarkers()) {
        EXPECT_EQ(coutText.find(marker), std::string::npos)
            << "std::cout must still carry CSV only; leaked: " << marker;
    }
}

// ================================================
// M5 — the disabled arm: the display keeps stdout
// ================================================

// With stdoutCsv=false the feature must be completely absent: the human display
// stays on stdout (the pre-feature behaviour) and no CSV appears there. This is
// the regression guard that adding the flag did not change the default run.
TEST_F(TelemetryRunnerStdoutCsvTest, StdoutCsvDisabledKeepsHumanDisplayOnStdout) {
    std::string displayText;
    int exitCode = 1;
    {
        const StreamCapture coutCapture(std::cout);
        auto source = std::make_unique<SelfStoppingSource>(stop_, drivingSignal(), 2, activity_);
        exitCode = TelemetryRunner::run(
            std::move(source), config_.get(),
            TelemetryRunOptions{.logCsvPath = "",
                                .logRawPath = "",
                                .pollIntervalMs = 1,
                                .stdoutCsv = false},
            stop_);
        displayText = coutCapture.text();
    }

    ASSERT_EQ(exitCode, 0);
    EXPECT_NE(displayText.find("Starting"), std::string::npos)
        << "the run banner belongs on stdout when --stdout-csv is off";
    EXPECT_NE(displayText.find("Throttle:"), std::string::npos)
        << "the per-row human display belongs on stdout when --stdout-csv is off";
    EXPECT_EQ(displayText.find("timestamp_ms"), std::string::npos)
        << "no CSV may be emitted when --stdout-csv is off";
}

// Even with a stream wired up, the FLAG decides. A caller that leaves the
// stream set but turns the flag off must get no CSV — the factory's null-object
// arm, observed through the real run loop rather than in isolation.
TEST_F(TelemetryRunnerStdoutCsvTest, StdoutCsvDisabledWritesNothingToInjectedStream) {
    int exitCode = 1;
    {
        const StreamCapture coutCapture(std::cout);
        auto source = std::make_unique<SelfStoppingSource>(stop_, drivingSignal(), 2, activity_);
        exitCode = TelemetryRunner::run(
            std::move(source), config_.get(),
            TelemetryRunOptions{.logCsvPath = "",
                                .logRawPath = "",
                                .pollIntervalMs = 1,
                                .stdoutCsv = false,
                                .stdoutCsvStream = &csvOut_},
            stop_);
    }

    ASSERT_EQ(exitCode, 0);
    EXPECT_TRUE(csvOut_.str().empty())
        << "stdoutCsv=false must emit nothing, even with a stream injected";
}
