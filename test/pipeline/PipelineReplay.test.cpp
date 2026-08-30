#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/BinaryFileSource.h"
#include "vehicle-sim/pipeline/LiveTwaiSource.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

namespace {

class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("vhsim_e2etest_" + std::to_string(counter_++))) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] std::string base(const std::string& name) const {
        return (path_ / name).string();
    }
    [[nodiscard]] std::string writeCapture(const std::string& name,
                                           const std::string& content) const {
        auto p = (path_ / name).string();
        std::ofstream out(p);
        out << content;
        return p;
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

// Minimal in-memory transport that yields each scripted line once (mirrors the
// LiveTwaiSource test helper) — the fake "live wire" for raw-capture tests.
class ScriptedTransport final : public ITransport {
public:
    explicit ScriptedTransport(std::vector<std::string> lines)
        : lines_(std::move(lines)) {}
    bool open() override { return true; }
    [[nodiscard]] bool isOpen() const noexcept override { return idx_ < lines_.size(); }
    std::optional<std::string> nextLine() override {
        if (idx_ >= lines_.size()) return std::nullopt;
        return lines_[idx_++];
    }
private:
    std::vector<std::string> lines_;
    std::size_t idx_ = 0;
};

} // namespace

// ============================================================
// PipelineReplay — IFrameSource-driven pipeline. The defining invariant: file
// replay writes ONLY <base>.csv and NEVER <base>.raw.txt (input file is the
// raw source of truth).
// ============================================================

TEST(PipelineReplayTest, FileReplayWritesOnlyCsv_NoRawTxt) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap.csv",
        "timestamp_ms,raw_line\n"
        "1000,118 3C 00 18 00 04 A0 01 FF\n"
        "\n"                                  // blank -> skipped
        "1000,TWAI started @ 500kbps\n"       // status -> skipped
        "999,GG 00 11 22 33\n"                 // malformed -> counted
        "2000,118 3C 00 18 00 04 A0 01 FF\n"
    );

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(source, service, ReplayOutputs{.decoded = &sink});

    EXPECT_TRUE(dir.exists("out.csv"));
    EXPECT_FALSE(dir.exists("out.raw.txt"));

    // 2 frames decoded; the legacy CSV header is skipped (no comma split
    // would have a ts and ASCII payload), the status and blank lines are
    // skipped, the malformed line is skipped.
    EXPECT_EQ(stats.framesDecoded, 2u);
    EXPECT_GT(stats.linesRead, 0u);
}

TEST(PipelineReplayTest, LivePathTimestampsComeFromWallClock) {
    // Live path: LiveTwaiSource stamps wall-clock on each frame. The decoded
    // CSV's timestamp_ms must be a recent epoch-ms value, not zero.
    TempDir dir;
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    DemoTransport transport(60);
    ASSERT_TRUE(transport.open());
    LiveTwaiSource source(transport);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("live_ts");
    DecodedCsvSink sink(base);

    auto before = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto stats = runReplay(source, service, ReplayOutputs{.decoded = &sink});

    auto after = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    EXPECT_GT(stats.framesDecoded, 0u);

    auto csv = dir.read("live_ts.csv");
    std::istringstream ss(csv);
    std::string line;
    std::getline(ss, line); // skip header

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        auto commaPos = line.find(',');
        EXPECT_NE(commaPos, std::string::npos);
        std::uint64_t ts = 0;
        auto res = std::from_chars(line.data(), line.data() + commaPos, ts, 10);
        EXPECT_TRUE(res.ec == std::errc{});
        EXPECT_GT(ts, 0u) << "live CSV timestamp must not be 0";
        EXPECT_GE(ts, before) << "live CSV timestamp must be >= test start";
        EXPECT_LE(ts, after + 60000u) << "live CSV timestamp must be <= test end + tolerance";
    }
}

TEST(PipelineReplayTest, NullDecodedSinkRunsDecodeWithoutOutput) {
    // Pipeline must tolerate a null decoded sink (decode-disabled replay).
    TempDir dir;
    std::string capture = dir.writeCapture("cap3.csv",
        "1000,118 3C 00 18 00 04 A0 01 FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    auto stats = runReplay(source, service, ReplayOutputs{});
    EXPECT_GT(stats.linesRead, 0u);
    EXPECT_FALSE(dir.exists("anything.csv"));
}

TEST(PipelineReplayTest, FrameWithUnknownCanId_StillCountsAsDecoded) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_unknown.csv",
        "1000,123 3C 00 18 00 04 A0 01 FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    auto stats = runReplay(source, service, ReplayOutputs{});

    EXPECT_GT(stats.linesRead, 0u);
    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_EQ(stats.skippedLines, 0u);
    EXPECT_EQ(stats.malformedLines, 0u);
}

TEST(PipelineReplayTest, NullProgressReporter_DecodesAndWritesCsvWithoutDeref) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_noprogress.csv",
        "1000,118 3C 00 18 00 04 A0 01 FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("noprogress");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(source, service, ReplayOutputs{.decoded = &sink});

    EXPECT_GT(stats.linesRead, 0u);
    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_TRUE(dir.exists("noprogress.csv"));
}

TEST(PipelineReplayTest, NullRawSink_WritesDecodedCsvAndRecordsNoRaw) {
    // The new IFrameSource pipeline does not record a raw sink for file
    // replay (the input file is the source of truth). The test pins that
    // contract: a null raw sink with a non-null decoded sink writes the
    // decoded CSV and never a .raw.txt.
    TempDir dir;
    std::string capture = dir.writeCapture("cap_noraw.csv",
        "1000,118 3C 00 18 00 04 A0 01 FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("noraw");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(source, service, ReplayOutputs{.decoded = &sink});

    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_TRUE(dir.exists("noraw.csv"));
    EXPECT_FALSE(dir.exists("noraw.raw.txt"));
}

// ============================================================
// LIVE path raw capture — the counterpart invariant to
// FileReplayWritesOnlyCsv_NoRawTxt: a LIVE source (transport behind
// LiveTwaiSource) wired to BOTH sinks must write <base>.raw.txt (the
// verbatim transport line, "<utc_ms>,<line>", captured BEFORE decode) AND
// <base>.csv.
// ============================================================
namespace {

constexpr const char* kLiveFrameLine = "118 3C 00 18 00 04 A0 01 FF";

// Drive a scripted "live wire" through the exact wiring LiveRunContext uses:
// LiveTwaiSource + decoded sink + raw sink. Returns the raw.txt path.
std::string runLiveCapture(const std::vector<std::string>& transportLines,
                           const std::string& base,
                           DBCTranslationService& service,
                           ReplayStats& statsOut) {
    ScriptedTransport transport(transportLines);
    LiveTwaiSource source(transport);
    EXPECT_TRUE(source.open());

    DecodedCsvSink decoded(base);
    EXPECT_TRUE(decoded.isValid());
    RawLogSink raw(base);
    EXPECT_TRUE(raw.isValid());

    statsOut = runReplay(source, service,
                         ReplayOutputs{.decoded = &decoded, .raw = &raw});
    return base + ".raw.txt";
}

// Drop the leading "<ts>," / "<ts>," field so live-vs-replay outputs can be
// compared without their (necessarily different) timestamps.
std::vector<std::string> csvRowsWithoutTimestamp(const std::string& csv) {
    std::vector<std::string> rows;
    std::istringstream ss(csv);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        const auto comma = line.find(',');
        rows.push_back(comma == std::string::npos ? line : line.substr(comma + 1));
    }
    return rows;
}

} // namespace

TEST(PipelineReplayTest, LiveSource_WritesRawTxtVerbatimAndCsv) {
    TempDir dir;
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    const std::vector<std::string> lines{
        kLiveFrameLine,
        "TWAI started @ 500kbps",  // non-frame status line: never a TwaiFrame
        kLiveFrameLine,
        kLiveFrameLine,
    };
    const std::string base = dir.base("live");

    ReplayStats stats;
    const std::string rawPath = runLiveCapture(lines, base, service, stats);

    // BOTH live artifacts exist; every captured line is the verbatim frame
    // line prefixed by an epoch-ms timestamp.
    ASSERT_TRUE(dir.exists("live.raw.txt")) << "live run must write <base>.raw.txt";
    ASSERT_TRUE(dir.exists("live.csv")) << "live run must write <base>.csv";
    EXPECT_EQ(stats.framesDecoded, 3u);

    std::istringstream raw(dir.read("live.raw.txt"));
    std::string row;
    std::size_t rawRows = 0;
    while (std::getline(raw, row)) {
        if (row.empty()) continue;
        ++rawRows;
        const auto comma = row.find(',');
        ASSERT_NE(comma, std::string::npos) << "raw row must be \"<ts>,<line>\": " << row;
        const auto ts = row.substr(0, comma);
        EXPECT_FALSE(ts.empty()) << "timestamp must be present: " << row;
        EXPECT_EQ(ts.find_first_not_of("0123456789"), std::string::npos)
            << "timestamp must be all decimal digits: " << row;
        EXPECT_GT(ts.size(), 10u) << "epoch-ms timestamps are ~13 digits: " << row;
        EXPECT_EQ(row.substr(comma + 1), kLiveFrameLine)
            << "raw row must carry the verbatim transport line: " << row;
    }
    EXPECT_EQ(rawRows, 3u) << "one raw row per FRAME (status text is not a frame)";
}

TEST(PipelineReplayTest, LiveRawTxt_RoundTripsThroughBinaryFileSource) {
    // The raw.txt a live run writes must be re-readable by BinaryFileSource
    // and decode to the SAME signals as the live decode (timestamps excluded —
    // the live path stamps wall-clock at capture, the replay re-reads the
    // written one). NOTE: the translator accumulates per-CAN-ID state across a
    // run (each VehicleSignal is a whole-vehicle snapshot), so the replay leg
    // uses a FRESH service — exactly what a real later replay (new process)
    // would have.
    TempDir dir;
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    const std::vector<std::string> lines{
        kLiveFrameLine,
        "257 00 01 02 03 04 05 06 07",
        kLiveFrameLine,
    };

    ReplayStats liveStats;
    const std::string rawPath = runLiveCapture(lines, dir.base("live"), service, liveStats);
    ASSERT_EQ(liveStats.framesDecoded, 3u);

    // Replay the captured raw.txt through the file path, fresh decoder state.
    DBCTranslationService replayService;
    DefaultVehicleConfigs::registerAll(replayService.registry());
    ASSERT_TRUE(replayService.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource replaySource(rawPath);
    ASSERT_TRUE(replaySource.open());
    DecodedCsvSink replaySink(dir.base("replayed"));
    ASSERT_TRUE(replaySink.isValid());

    auto replayStats = runReplay(replaySource, replayService,
                                 ReplayOutputs{.decoded = &replaySink});

    EXPECT_EQ(replayStats.framesDecoded, liveStats.framesDecoded)
        << "raw.txt must round-trip to the same decoded frame count";
    EXPECT_EQ(csvRowsWithoutTimestamp(dir.read("replayed.csv")),
              csvRowsWithoutTimestamp(dir.read("live.csv")))
        << "decoded signals must be identical after the raw.txt round-trip";
}
