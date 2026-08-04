// CsvStdoutReporters.test.cpp — the two reporter seams that carry --stdout-csv
// through the canonical replay pipeline.
//
// runReplay() notifies exactly one IProgressReporter. --stdout-csv needs two
// observers at once (the human view on stderr, the CSV on stdout), so the
// feature is built from two small pieces instead of a branch inside the loop:
//
//   CsvStdoutReporter          adapts an ICsvStdoutSink onto IProgressReporter
//   CompositeProgressReporter  fans one frame out to several reporters
//
// These tests pin the behaviour each piece promises the other. They assert CSV
// SHAPE and named-column values rather than literal rows — the exact bytes are
// already pinned against the real TraceLogger in CsvStdoutSink.test.cpp, and
// restating them here would only duplicate that coupling.

#include <gtest/gtest.h>

#include "../telemetry/CsvShape.h"

#include "vehicle-sim/pipeline/CompositeProgressReporter.h"
#include "vehicle-sim/pipeline/CsvStdoutReporter.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;
using vehicle_sim::telemetry::CsvStdoutSink;
using vehicle_sim::test::cellsByColumn;
using vehicle_sim::test::CSV_FIELD_COUNT;
using vehicle_sim::test::splitFields;
using vehicle_sim::test::splitLines;

namespace {

/// Records the calls it receives into a SHARED log. A fake, not a mock: the
/// tests assert observable behaviour rather than call expectations.
///
/// The shared log is the point — per-child counters can only prove that each
/// child was called, never the ORDER ACROSS children. Appending "<name>:frame"
/// to one sequence is what makes "delegates in registration order" an actual
/// assertion instead of a comment.
class RecordingReporter final : public IProgressReporter {
public:
    RecordingReporter(std::vector<std::string>& log, std::string name)
        : log_(log), name_(std::move(name)) {}

    void onFrame(const VehicleSignal& signal,
                 std::size_t frameIndex,
                 std::size_t totalHints) noexcept override {
        ++frameCalls;
        lastFrameIndex = frameIndex;
        lastTotalHints = totalHints;
        lastTimestamp = signal.getTimestampUtcMs();
        log_.push_back(name_ + ":frame");
    }

    void onComplete(const ReplayStats& stats) noexcept override {
        ++completeCalls;
        lastStats = stats;
        log_.push_back(name_ + ":complete");
    }

    std::size_t frameCalls = 0;
    std::size_t completeCalls = 0;
    std::size_t lastFrameIndex = 0;
    std::size_t lastTotalHints = 0;
    std::uint64_t lastTimestamp = 0;
    ReplayStats lastStats{};

private:
    std::vector<std::string>& log_;
    std::string name_;
};

VehicleSignal signalAt(std::uint64_t timestampMs, double speedKmh) {
    return VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = timestampMs,
        .speedKmh = speedKmh,
        .gearSelector = Gear::AUTO_1});
}

} // namespace

// ================================================
// M2 — CsvStdoutReporter: frame -> one CSV row, completion stays silent
// ================================================

class CsvStdoutReporterTest : public ::testing::Test {
protected:
    std::ostringstream out;
};

// Contract: each decoded frame the replay loop reports becomes exactly one CSV
// row on the sink's stream. This is what puts --stdout-csv on the canonical
// pipeline without runReplay() knowing the feature exists.
TEST_F(CsvStdoutReporterTest, OnFrameForwardsOneRowToTheSink) {
    CsvStdoutSink sink(out);
    CsvStdoutReporter reporter(sink);

    reporter.onFrame(signalAt(4242ULL, 88.0), 0U, 100U);

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 2U);  // header (written at sink construction) + 1 row
    EXPECT_EQ(splitFields(lines[1]).size(), CSV_FIELD_COUNT);

    const auto cells = cellsByColumn(lines[0], lines[1]);
    EXPECT_EQ(cells.at("timestamp_ms"), "4242");
    EXPECT_EQ(cells.at("speed_kmh"), "88.00");
    EXPECT_EQ(cells.at("gear_selector"), "D");
}

// The frame INDEX and the total hint are presentation concerns for the console
// reporter; a CSV stream must not gain a column or a comment because of them.
TEST_F(CsvStdoutReporterTest, RowContentIsIndependentOfFrameIndexAndHints) {
    CsvStdoutSink sink(out);
    CsvStdoutReporter reporter(sink);

    reporter.onFrame(signalAt(1000ULL, 10.0), 0U, 0U);
    reporter.onFrame(signalAt(1000ULL, 10.0), 999U, 65000U);

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[1], lines[2]);
}

// One row per frame, in arrival order — a consumer treats row order as frame
// order, so the adapter must neither reorder, batch, nor drop.
TEST_F(CsvStdoutReporterTest, EmitsOneRowPerFrameInOrder) {
    CsvStdoutSink sink(out);
    CsvStdoutReporter reporter(sink);

    reporter.onFrame(signalAt(1ULL, 1.0), 0U, 3U);
    reporter.onFrame(signalAt(2ULL, 2.0), 1U, 3U);
    reporter.onFrame(signalAt(3ULL, 3.0), 2U, 3U);

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 4U);  // header + 3 rows
    EXPECT_EQ(cellsByColumn(lines[0], lines[1]).at("timestamp_ms"), "1");
    EXPECT_EQ(cellsByColumn(lines[0], lines[2]).at("timestamp_ms"), "2");
    EXPECT_EQ(cellsByColumn(lines[0], lines[3]).at("timestamp_ms"), "3");
}

// onComplete() must add NOTHING: a trailing summary line would be a malformed
// record for any downstream CSV parser. The stream ends at its last data row.
TEST_F(CsvStdoutReporterTest, OnCompleteIsNoOpAndLeavesStreamUnchanged) {
    CsvStdoutSink sink(out);
    CsvStdoutReporter reporter(sink);
    reporter.onFrame(signalAt(7ULL, 7.0), 0U, 1U);

    const std::string beforeComplete = out.str();
    reporter.onComplete(ReplayStats{
        .linesRead = 10, .framesDecoded = 7, .malformedLines = 2, .skippedLines = 1});

    EXPECT_EQ(out.str(), beforeComplete);
}

// A run that decoded nothing must still leave a parseable, header-only stream —
// not an empty document and not a summary line.
TEST_F(CsvStdoutReporterTest, CompleteWithoutFramesLeavesHeaderOnlyStream) {
    CsvStdoutSink sink(out);
    CsvStdoutReporter reporter(sink);

    reporter.onComplete(ReplayStats{});

    auto lines = splitLines(out.str());
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(splitFields(lines[0]).size(), CSV_FIELD_COUNT);
}

// ================================================
// M3 — CompositeProgressReporter: fan-out, in order, null-tolerant
// ================================================

class CompositeProgressReporterTest : public ::testing::Test {
protected:
    std::vector<std::string> callLog;
};

// Contract: every registered child sees every frame, with the arguments passed
// through untouched. This is what lets --stdout-csv run the console view and
// the CSV emitter simultaneously off one notification.
TEST_F(CompositeProgressReporterTest, FansFramesOutToEveryChild) {
    RecordingReporter first(callLog, "first");
    RecordingReporter second(callLog, "second");

    CompositeProgressReporter composite;
    composite.add(&first);
    composite.add(&second);

    composite.onFrame(signalAt(555ULL, 25.0), 3U, 42U);

    EXPECT_EQ(first.frameCalls, 1U);
    EXPECT_EQ(second.frameCalls, 1U);

    // A child must see exactly what runReplay() reported, not a re-derived value.
    EXPECT_EQ(first.lastTimestamp, 555ULL);
    EXPECT_EQ(first.lastFrameIndex, 3U);
    EXPECT_EQ(first.lastTotalHints, 42U);
    EXPECT_EQ(second.lastTimestamp, 555ULL);
    EXPECT_EQ(second.lastFrameIndex, 3U);
    EXPECT_EQ(second.lastTotalHints, 42U);
}

// Delegation order is registration order. It matters when children share a
// destination (e.g. two views on one terminal): the output must be
// deterministic. The shared log is what makes this observable.
TEST_F(CompositeProgressReporterTest, DelegatesInRegistrationOrder) {
    RecordingReporter first(callLog, "first");
    RecordingReporter second(callLog, "second");
    RecordingReporter third(callLog, "third");

    CompositeProgressReporter composite;
    composite.add(&first);
    composite.add(&second);
    composite.add(&third);

    composite.onFrame(signalAt(1ULL, 1.0), 0U, 0U);

    const std::vector<std::string> expected{"first:frame", "second:frame", "third:frame"};
    EXPECT_EQ(callLog, expected);
}

// Each frame is fully fanned out before the next begins, and order is stable
// across frames — a consumer pairing the two children's output relies on it.
TEST_F(CompositeProgressReporterTest, PreservesOrderAcrossSuccessiveFrames) {
    RecordingReporter first(callLog, "first");
    RecordingReporter second(callLog, "second");

    CompositeProgressReporter composite;
    composite.add(&first);
    composite.add(&second);

    composite.onFrame(signalAt(1ULL, 1.0), 0U, 2U);
    composite.onFrame(signalAt(2ULL, 2.0), 1U, 2U);

    const std::vector<std::string> expected{
        "first:frame", "second:frame", "first:frame", "second:frame"};
    EXPECT_EQ(callLog, expected);
    EXPECT_EQ(first.frameCalls, 2U);
    EXPECT_EQ(second.frameCalls, 2U);
    EXPECT_EQ(first.lastTimestamp, 2ULL);
    EXPECT_EQ(second.lastTimestamp, 2ULL);
}

// onComplete() fans out too, in the same order — a child that summarises a run
// must not be skipped because it was registered last.
TEST_F(CompositeProgressReporterTest, FansCompletionOutToEveryChildInOrder) {
    RecordingReporter first(callLog, "first");
    RecordingReporter second(callLog, "second");

    CompositeProgressReporter composite;
    composite.add(&first);
    composite.add(&second);

    composite.onComplete(ReplayStats{
        .linesRead = 9, .framesDecoded = 8, .malformedLines = 1, .skippedLines = 0});

    EXPECT_EQ(first.completeCalls, 1U);
    EXPECT_EQ(second.completeCalls, 1U);
    EXPECT_EQ(first.lastStats.framesDecoded, 8U);
    EXPECT_EQ(second.lastStats.linesRead, 9U);

    const std::vector<std::string> expected{"first:complete", "second:complete"};
    EXPECT_EQ(callLog, expected);
}

// A null child is IGNORED, not stored: that is what lets a caller register an
// optional reporter (e.g. "CSV only when --stdout-csv") without branching. The
// real children must still be served in order — a null must not truncate the
// fan-out nor shift the sequence.
TEST_F(CompositeProgressReporterTest, IgnoresNullChildAndStillServesTheRest) {
    RecordingReporter first(callLog, "first");
    RecordingReporter second(callLog, "second");

    CompositeProgressReporter composite;
    composite.add(nullptr);   // optional reporter absent
    composite.add(&first);
    composite.add(nullptr);   // another absent one
    composite.add(&second);

    composite.onFrame(signalAt(2ULL, 2.0), 0U, 0U);
    composite.onComplete(ReplayStats{});

    EXPECT_EQ(first.frameCalls, 1U);
    EXPECT_EQ(second.frameCalls, 1U);
    EXPECT_EQ(first.completeCalls, 1U);
    EXPECT_EQ(second.completeCalls, 1U);

    const std::vector<std::string> expected{
        "first:frame", "second:frame", "first:complete", "second:complete"};
    EXPECT_EQ(callLog, expected);
}

// Degenerate arms of the same contract: a composite with no children (or only
// null ones) is a valid silent reporter, so a caller never needs a null check.
TEST_F(CompositeProgressReporterTest, EmptyCompositeIsSilentAndSafe) {
    CompositeProgressReporter composite;

    EXPECT_NO_THROW(composite.onFrame(signalAt(1ULL, 1.0), 0U, 0U));
    EXPECT_NO_THROW(composite.onComplete(ReplayStats{}));
    EXPECT_TRUE(callLog.empty());
}

TEST_F(CompositeProgressReporterTest, CompositeOfOnlyNullChildrenIsSilentAndSafe) {
    CompositeProgressReporter composite;
    composite.add(nullptr);
    composite.add(nullptr);

    EXPECT_NO_THROW(composite.onFrame(signalAt(1ULL, 1.0), 0U, 0U));
    EXPECT_NO_THROW(composite.onComplete(ReplayStats{}));
    EXPECT_TRUE(callLog.empty());
}

// The two pieces composed as production wires them: the CSV reporter is one
// leaf beside the console view. The CSV stream must carry the data rows and
// stay free of the other child's output.
TEST_F(CompositeProgressReporterTest, ComposedWithCsvReporterKeepsCsvStreamClean) {
    std::ostringstream csvOut;
    CsvStdoutSink sink(csvOut);
    CsvStdoutReporter csvReporter(sink);
    RecordingReporter console(callLog, "console");

    CompositeProgressReporter composite;
    composite.add(&console);
    composite.add(&csvReporter);

    composite.onFrame(signalAt(11ULL, 30.0), 0U, 2U);
    composite.onFrame(signalAt(22ULL, 60.0), 1U, 2U);
    composite.onComplete(ReplayStats{.linesRead = 2, .framesDecoded = 2});

    EXPECT_EQ(console.frameCalls, 2U);
    EXPECT_EQ(console.completeCalls, 1U);

    // header + exactly the 2 data rows: onComplete() contributed nothing.
    auto lines = splitLines(csvOut.str());
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(cellsByColumn(lines[0], lines[1]).at("timestamp_ms"), "11");
    EXPECT_EQ(cellsByColumn(lines[0], lines[2]).at("timestamp_ms"), "22");
}
