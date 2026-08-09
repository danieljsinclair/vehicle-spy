#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/pipeline/ReplayPacing.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/util/IClock.h"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::util;

namespace {

// A recording clock that forwards to a FakeClock but records every sleepFor
// duration. Lets the paced-replay test assert inter-arrival spacing WITHOUT
// real wall-clock time (the FakeClock makes pacing instant + deterministic).
class RecordingClock final : public IClock {
public:
    RecordingClock() : fake_() {}
    [[nodiscard]] time_point now() const override { return fake_.now(); }
    void sleepFor(std::chrono::milliseconds d) override {
        sleeps_.push_back(d);
        fake_.sleepFor(d);
    }
    void advance(duration d) { fake_.advance(d); }
    const std::vector<std::chrono::milliseconds>& sleeps() const { return sleeps_; }

protected:
    [[nodiscard]] bool waitForImpl(std::condition_variable&, std::unique_lock<std::mutex>&,
                                   const std::function<bool()>&, time_point) const override {
        return true; // never parked in this test
    }
private:
    FakeClock fake_;
    std::vector<std::chrono::milliseconds> sleeps_;
};

class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("vs_replaypacing_" + std::to_string(counter_++))) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] std::string writeCapture(const std::string& name,
                                           const std::string& content) const {
        auto p = (path_ / name).string();
        std::ofstream out(p);
        out << content;
        return p;
    }
    [[nodiscard]] std::string base(const std::string& name) const {
        return (path_ / name).string();
    }
    [[nodiscard]] bool exists(const std::string& rel) const noexcept {
        return std::filesystem::exists(path_ / rel);
    }
    std::string read(const std::string& rel) const {
        std::ifstream in(path_ / rel);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
private:
    std::filesystem::path path_;
    static int counter_;
};
int TempDir::counter_ = 0;

// Build a small, real-shaped capture with frames at recorded timestamps.
std::string makeCapture(const std::vector<std::uint64_t>& tsMs) {
    std::ostringstream out;
    out << "timestamp_ms,can_id,dlc,data_hex\n";
    for (auto ts : tsMs) {
        out << ts << ",118,8,3C00180004A001FF\n";
    }
    return out.str();
}

// Parse the decoded CSV, returning the timestamp_ms column of each data row.
std::vector<std::uint64_t> decodedTimestamps(const std::string& csv) {
    std::vector<std::uint64_t> out;
    std::istringstream ss(csv);
    std::string line;
    std::getline(ss, line); // header
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        auto comma = line.find(',');
        EXPECT_NE(comma, std::string::npos);
        std::uint64_t ts = 0;
        auto res = std::from_chars(line.data(), line.data() + comma, ts, 10);
        EXPECT_TRUE(res.ec == std::errc{});
        out.push_back(ts);
    }
    return out;
}

// --- ReplayPacing unit (mirrors engine-sim-cli classifyRow/setStartFromS) ---

TEST(ReplayPacingTest, SetAndReadStartFromS) {
    ReplayPacing pacing;
    EXPECT_DOUBLE_EQ(pacing.startFromS(), -1.0);
    pacing.setStartFromS(2.5);
    EXPECT_DOUBLE_EQ(pacing.startFromS(), 2.5);
}

TEST(ReplayPacingTest, BlankFrameDetected) {
    RawFrame blank;
    blank.timestampMs = 0;
    blank.dlc = 0;
    EXPECT_TRUE(ReplayPacing::isFrameBlank(blank));

    RawFrame real;
    real.timestampMs = 1000;
    real.dlc = 8;
    EXPECT_FALSE(ReplayPacing::isFrameBlank(real));

    // A frame with a timestamp but zero payload is NOT blank (it has a time).
    RawFrame timed;
    timed.timestampMs = 500;
    timed.dlc = 0;
    EXPECT_FALSE(ReplayPacing::isFrameBlank(timed));
}

TEST(ReplayPacingTest, ClassifySurfacesFirstRowImmediately) {
    ReplayPacing pacing;
    RawFrame f;
    f.timestampMs = 1000;
    // baseline == frame ts -> scheduled 0 -> surface now (0).
    EXPECT_EQ(pacing.classifyFrame(f, /*baselineTsMs=*/1000, /*elapsed=*/0), 0);
}

TEST(ReplayPacingTest, ClassifyFutureRowReturnsWaitMs) {
    ReplayPacing pacing;
    RawFrame f;
    f.timestampMs = 1000;
    // baseline 0, elapsed 0, scheduled 1000 -> behind 1000 -> wait 1000.
    EXPECT_EQ(pacing.classifyFrame(f, /*baselineTsMs=*/0, /*elapsed=*/0), 1000);
}

TEST(ReplayPacingTest, ClassifyBeforeStartFromSkipped) {
    ReplayPacing pacing(/*startFromS=*/2.0); // 2000 ms threshold
    RawFrame early;
    early.timestampMs = 1500;
    // before --start-from -> Skip (-1).
    EXPECT_EQ(pacing.classifyFrame(early, /*baselineTsMs=*/0, /*elapsed=*/0), -1);
    RawFrame late;
    late.timestampMs = 2500;
    // at/after threshold -> not skipped (future/surface decision applies).
    EXPECT_NE(pacing.classifyFrame(late, /*baselineTsMs=*/0, /*elapsed=*/0), -1);
}

// --- Integration: paced replay via runReplay + RecordingClock ---

TEST(ReplayPacingIntegrationTest, PacedReplaySpacesRowsByRecordedTimestamps) {
    TempDir dir;
    // Frames at 0, 1000, 2000, 3000 ms -> paced replay should sleep ~1000ms
    // (virtual) between each emitted row.
    auto capture = dir.writeCapture("paced.csv", makeCapture({0, 1000, 2000, 3000}));

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(transport, normaliser, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/-1.0);

    // All 4 frames decoded.
    EXPECT_EQ(stats.framesDecoded, 4u);
    // 1 skip = the CSV header line (classed NotAFrame -> skip), as in unpaced mode.
    EXPECT_EQ(stats.skippedLines, 1u);

    // The clock recorded one sleep per inter-row gap (3 gaps for 4 rows), each
    // ~1000 ms (virtual). Exact because the FakeClock makes pacing deterministic.
    ASSERT_EQ(clock.sleeps().size(), 3u);
    for (const auto& s : clock.sleeps()) {
        EXPECT_EQ(s.count(), 1000) << "each inter-row gap should be 1000ms (recorded delta)";
    }

    // Decoded CSV preserves the recorded timestamps.
    auto ts = decodedTimestamps(dir.read("out.csv"));
    ASSERT_EQ(ts.size(), 4u);
    EXPECT_EQ(ts[0], 0u);
    EXPECT_EQ(ts[1], 1000u);
    EXPECT_EQ(ts[2], 2000u);
    EXPECT_EQ(ts[3], 3000u);
}

TEST(ReplayPacingIntegrationTest, PacedReplaySkipsBlankRows) {
    TempDir dir;
    // A blank row (zero ts + zero dlc) sits between two real frames. Paced
    // replay must skip it; live (Unpaced) would surface everything.
    std::string content =
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
        "0,000,0,\n"                 // blank placeholder -> skipped in replay
        "2000,118,8,3C00180004A001FF\n";
    auto capture = dir.writeCapture("blank.csv", content);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    {
        FileTransport transport(capture);
        ASSERT_TRUE(transport.open());
        CaptureNormaliser normaliser;
        std::string base = dir.base("paced_blank");
        DecodedCsvSink sink(base);
        ASSERT_TRUE(sink.isValid());

        RecordingClock clock;
        auto stats = runReplay(transport, normaliser, service,
                               ReplayOutputs{.decoded = &sink}, ReplayMode::Paced,
                               clock, -1.0);
        EXPECT_EQ(stats.framesDecoded, 2u) << "blank row must be skipped in replay";
        // 2 skips = 1 CSV header + 1 blank row.
        EXPECT_EQ(stats.skippedLines, 2u);
        auto ts = decodedTimestamps(dir.read("paced_blank.csv"));
        ASSERT_EQ(ts.size(), 2u);
        EXPECT_EQ(ts[0], 1000u);
        EXPECT_EQ(ts[1], 2000u);
    }

    // Control: Unpaced (live) replays the blank row too (no blank skipping).
    {
        FileTransport transport(capture);
        ASSERT_TRUE(transport.open());
        CaptureNormaliser normaliser;
        std::string base = dir.base("unpaced_blank");
        DecodedCsvSink sink(base);
        ASSERT_TRUE(sink.isValid());

        auto stats = runReplay(transport, normaliser, service,
                               ReplayOutputs{.decoded = &sink}, ReplayMode::Unpaced);
        EXPECT_EQ(stats.framesDecoded, 3u) << "live path must NOT skip blank rows";
    }
}

TEST(ReplayPacingIntegrationTest, PacedReplayStartFromSkipsEarlyRows) {
    TempDir dir;
    // Frames at 0, 1000, 2000, 3000 ms. start-from 2.0s should skip the first
    // two rows (0 and 1000 ms) and emit 2000 + 3000.
    auto capture = dir.writeCapture("startfrom.csv", makeCapture({0, 1000, 2000, 3000}));

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("sf");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(transport, normaliser, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/2.0);

    EXPECT_EQ(stats.framesDecoded, 2u);
    // Pins the `waitMs < 0 => ++skippedLines` translation: the CSV header plus
    // the two pre-threshold frames (0ms, 1000ms) are all counted as skipped.
    // Without this, an extraction could drop the pre-start-from rows silently
    // (still not decoding them, but no longer accounting for them).
    EXPECT_EQ(stats.skippedLines, 3u);
    auto ts = decodedTimestamps(dir.read("sf.csv"));
    ASSERT_EQ(ts.size(), 2u);
    EXPECT_EQ(ts[0], 2000u);
    EXPECT_EQ(ts[1], 3000u);
}

// --- Phase-1 characterisation tests (pre-refactor safety net) -------------
// These pin behaviour that the PacedFrameScheduler / ReplayOutputs extractions
// directly own. They pass on the un-refactored code by construction; their job
// is to FAIL if an extraction changes the observable contract.

// Contract: the pacing baseline anchors on the first NON-BLANK frame, not on
// the first frame of any kind. A leading blank row must not become the
// baseline.
//
// Why the sleep COUNT is the load-bearing assertion: with a correct baseline
// (anchored at 1000ms) the only scheduled gap is 1000->2000, so exactly one
// wait occurs. If a blank row wrongly anchored the baseline at 0ms, BOTH real
// frames would sit in the future (1000ms and 2000ms ahead) and the run would
// record two sleeps. The count discriminates the two baselines; the durations
// confirm which one was chosen.
TEST(ReplayPacingIntegrationTest, PacedReplay_AnchorsBaselineOnFirstNonBlankFrame) {
    TempDir dir;
    std::string content =
        "timestamp_ms,can_id,dlc,data_hex\n"
        "0,000,0,\n"                          // leading blank -> must NOT anchor
        "1000,118,8,3C00180004A001FF\n"       // first real frame -> the baseline
        "2000,118,8,3C00180004A001FF\n";
    auto capture = dir.writeCapture("baseline.csv", content);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("baseline_out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(transport, normaliser, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/-1.0);

    EXPECT_EQ(stats.framesDecoded, 2u) << "both real frames must decode";

    // Load-bearing: exactly one inter-frame wait proves the baseline anchored
    // on the 1000ms frame rather than on the leading blank.
    ASSERT_EQ(clock.sleeps().size(), 1u)
        << "a blank row must not anchor the pacing baseline";
    EXPECT_EQ(clock.sleeps().front().count(), 1000)
        << "the single wait should span the 1000ms->2000ms recorded gap";

    auto ts = decodedTimestamps(dir.read("baseline_out.csv"));
    ASSERT_EQ(ts.size(), 2u);
    EXPECT_EQ(ts[0], 1000u);
    EXPECT_EQ(ts[1], 2000u);
}

// Contract: in paced mode a malformed row is routed to malformedLines ONLY —
// it is not additionally counted as skipped, and it is not decoded. Pins the
// full routing rather than a single count, so a refactor cannot quietly
// reclassify malformed rows as skips.
TEST(ReplayPacingIntegrationTest, PacedReplay_MalformedRow_RoutedToMalformedNotSkippedOrDecoded) {
    TempDir dir;
    std::string content =
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
        "999,GG,8,00112233\n"                 // malformed CAN id
        "2000,118,8,3C00180004A001FF\n";
    auto capture = dir.writeCapture("malformed.csv", content);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("malformed_out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(transport, normaliser, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/-1.0);

    EXPECT_EQ(stats.malformedLines, 1u) << "the bad row must be counted malformed";
    EXPECT_EQ(stats.skippedLines, 1u)
        << "only the CSV header is a skip — malformed must not double-count";
    EXPECT_EQ(stats.framesDecoded, 2u) << "the malformed row must not decode";
}

// Contract: the raw sink records a row VERBATIM before the pacing/skip
// decision, so a row skipped for decode still appears in the raw capture.
//
// This is the ordering invariant the ReplayOutputs extraction can break: if
// the raw write were moved after the skip decision, skipped rows would vanish
// from the verbatim capture and the raw file would stop being a faithful
// replay source. Existing cover pinned this only in UNPACED mode.
TEST(ReplayPacingIntegrationTest, PacedReplay_RawSinkRecordsSkippedRowBeforeSkipDecision) {
    TempDir dir;
    std::string content =
        "timestamp_ms,can_id,dlc,data_hex\n"
        "0,000,0,\n"                          // blank -> skipped for decode
        "1000,118,8,3C00180004A001FF\n";
    auto capture = dir.writeCapture("raworder.csv", content);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("raworder_out");
    DecodedCsvSink decoded(base);
    ASSERT_TRUE(decoded.isValid());
    RawLogSink raw(base);
    ASSERT_TRUE(raw.isValid());

    RecordingClock clock;
    auto stats = runReplay(transport, normaliser, service,
                           ReplayOutputs{.decoded = &decoded, .raw = &raw},
                           ReplayMode::Paced, clock, /*startFromS=*/-1.0);

    // The blank row was genuinely skipped for decode...
    EXPECT_EQ(stats.framesDecoded, 1u) << "the blank row must not decode";

    // ...yet its verbatim text is still present in the raw capture, proving the
    // raw write happened BEFORE the skip decision.
    auto rawContent = dir.read("raworder_out.raw.txt");
    EXPECT_NE(rawContent.find("0,000,0,"), std::string::npos)
        << "a row skipped for decode must still be captured verbatim by the raw sink";
    EXPECT_NE(rawContent.find("1000,118,8,3C00180004A001FF"), std::string::npos)
        << "the decoded row must also be captured verbatim";
}

TEST(ReplayPacingIntegrationTest, EofTerminatesPacedReplayCleanly) {
    // EOF: FileTransport::nextLine() returns nullopt at EOF; the paced while
    // loop must exit cleanly (no hang, no crash). Confirmed with a real capture
    // and a FakeClock (which would expose any accidental infinite wait).
    TempDir dir;
    auto capture = dir.writeCapture("eof.csv", makeCapture({0, 500, 1500}));

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    FakeClock clock; // never advanced externally -> any sleepFor returns instantly
    auto stats = runReplay(transport, normaliser, service, ReplayOutputs{},
                           ReplayMode::Paced, clock, -1.0);
    EXPECT_EQ(stats.framesDecoded, 3u); // all rows emitted, loop exited at EOF
    EXPECT_EQ(stats.linesRead, 4u);    // header + 3 frames
}

} // namespace
