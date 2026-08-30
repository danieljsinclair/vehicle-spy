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
