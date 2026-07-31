#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"
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
// PipelineReplay — Phase 1 end-to-end. The defining Phase 1 invariant: file
// replay writes ONLY <base>.csv and NEVER <base>.raw.txt (input file is the
// raw source of truth).
// ============================================================

TEST(PipelineReplayTest, FileReplayWritesOnlyCsv_NoRawTxt) {
    TempDir dir;
    // A tiny but real-shaped capture (legacy CSV form).
    std::string capture = dir.writeCapture("cap.csv",
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
        "\n"                                  // blank -> skipped
        "1000,TWAI started @ 500kbps\n"       // status -> skipped
        "999,GG,8,00112233\n"                 // malformed -> counted
        "2000,225,3,AABBCC\n"
    );

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(transport, normaliser, service, &sink, /*rawSink=*/nullptr);

    // Invariants for Phase 1 file replay.
    EXPECT_TRUE(dir.exists("out.csv"));
    EXPECT_FALSE(dir.exists("out.raw.txt"));

    // Stats: 6 lines read (header + frame + blank + status + malformed + frame),
    // 2 frames decoded, 3 skipped (header + blank + status), 1 malformed.
    EXPECT_EQ(stats.linesRead, 6u);
    EXPECT_EQ(stats.malformedLines, 1u);
    EXPECT_EQ(stats.skippedLines, 3u);
}

TEST(PipelineReplayTest, RawSinkWhenProvidedRecordsVerbatimLines) {
    // When a raw sink IS supplied (Phase 2 live transports), it captures every
    // transport line. Phase 1 file replay never supplies one, but the seam must
    // work when wired.
    TempDir dir;
    std::string capture = dir.writeCapture("cap2.csv",
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
    );

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("live");
    DecodedCsvSink decoded(base);
    RawLogSink raw(base);

    auto stats = runReplay(transport, normaliser, service, &decoded, &raw);
    EXPECT_GE(stats.linesRead, 1u);

    EXPECT_TRUE(dir.exists("live.csv"));
    EXPECT_TRUE(dir.exists("live.raw.txt"));
    // Raw sink now prefixes every line with "<utc_ms>,", so the original
    // transport text follows the first comma on each row.
    auto rawContent = dir.read("live.raw.txt");
    EXPECT_NE(rawContent.find("timestamp_ms,can_id,dlc,data_hex"), std::string::npos);
    EXPECT_NE(rawContent.find("1000,118,8,3C00180004A001FF"), std::string::npos);
}

TEST(PipelineReplayTest, ReplayPreservesCaptureTimestampsInCsv) {
    // Replay path: capture-file timestamps must appear in the decoded CSV's
    // timestamp_ms column (not wall-clock, not zero).
    TempDir dir;
    std::string capture = dir.writeCapture("ts_cap.csv",
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
        "2000,118,8,3C00180004A001FF\n"
    );

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("ts_out");
    DecodedCsvSink sink(base);

    auto stats = runReplay(transport, normaliser, service, &sink, nullptr);
    EXPECT_EQ(stats.framesDecoded, 2u);

    auto csv = dir.read("ts_out.csv");
    // Both data rows must contain the capture timestamps (1000 and 2000).
    EXPECT_NE(csv.find("1000,"), std::string::npos) << "first frame timestamp 1000 must appear";
    EXPECT_NE(csv.find("2000,"), std::string::npos) << "second frame timestamp 2000 must appear";
    // Neither row should have timestamp 0 (which would indicate the live path
    // bug where RawFrameNormaliser stamped all frames with timestampMs=0).
    {
        std::istringstream ss(csv);
        std::string line;
        std::getline(ss, line); // skip header
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            EXPECT_NE(line.substr(0, 2), "0,")
                << "replay CSV row must not start with '0,' (timestamp zero): " << line;
        }
    }
}

TEST(PipelineReplayTest, LivePathTimestampsComeFromWallClock) {
    // Live path (RawFrameNormaliser): the decoder stamps signals with wall-clock
    // because the normaliser does not set hasTimestamp.
    TempDir dir;
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    DemoTransport transport(60);
    ASSERT_TRUE(transport.open());
    RawFrameNormaliser normaliser;

    std::string base = dir.base("live_ts");
    DecodedCsvSink sink(base);

    auto before = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto stats = runReplay(transport, normaliser, service, &sink, nullptr);

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
        // First CSV field (timestamp_ms) must parse as a non-zero integer within
        // the before/after window.
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
        "1000,118,8,3C00180004A001FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    auto stats = runReplay(transport, normaliser, service,
                           /*decodedSink=*/nullptr, /*rawSink=*/nullptr);
    EXPECT_EQ(stats.linesRead, 1u);
    // A frame-shaped line still decodes (we just don't persist it).
    EXPECT_FALSE(dir.exists("anything.csv"));
}

// --- Blind characterisation contracts for S3776/S134 (runReplay loop) ---
// These lock the externally-observable null-sink behaviour the runReplay loop
// must preserve under refactor: each optional sink/reporter may be null without
// fault, and a well-formed frame-shaped line is always counted as decoded.
//
// NOTE on the gate-report's "unknown CAN id decodes to no signal" contract:
// that precondition is UNREACHABLE through this pipeline. DBCSignalTranslator::
// translate() returns nullopt ONLY for a sub-min-length packet
// (isValidPacket: rawData.size() < 10); for any well-formed frame it stores the
// payload and delegates to VehicleSignalFactory::build(), which ALWAYS returns
// a (possibly all-nullopt-field) VehicleSignal. processFrame() therefore never
// returns nullopt for a normalised frame, so runReplay's "signal falsy → skip
// framesDecoded" arm never fires for valid frames — every well-formed frame
// increments framesDecoded regardless of CAN id. Contract 1 below locks that
// real behaviour (flagged to team-lead as a gate-report discrepancy).

TEST(PipelineReplayTest, FrameWithUnknownCanId_StillCountsAsDecoded) {
    // Contract (recast from the gate report after tracing): a frame-shaped,
    // well-normalised line whose CAN id is absent from the loaded DBC is still
    // counted as decoded — the CAN id is not a decode filter in this pipeline.
    // linesRead and framesDecoded both increment; it is neither skipped nor
    // malformed. Locks the actual behaviour; the "decode to no signal" variant
    // is unreachable (see module note above).
    TempDir dir;
    // id 4321 (0x4321) is not carried by the tesla DBC; bytes are well-formed.
    std::string capture = dir.writeCapture("cap_unknown.csv",
        "1000,4321,8,3C00180004A001FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    auto stats = runReplay(transport, normaliser, service,
                           /*decodedSink=*/nullptr, /*rawSink=*/nullptr);

    EXPECT_EQ(stats.linesRead, 1u);
    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_EQ(stats.skippedLines, 0u);
    EXPECT_EQ(stats.malformedLines, 0u);
}

TEST(PipelineReplayTest, NullProgressReporter_DecodesAndWritesCsvWithoutDeref) {
    // Contract: a null progress reporter (explicit nullptr) with a non-null
    // decoded sink writes the decoded CSV, reports correct stats, and does not
    // dereference the null reporter on either the per-frame or completion path.
    TempDir dir;
    std::string capture = dir.writeCapture("cap_noprogress.csv",
        "1000,118,8,3C00180004A001FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("noprogress");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(transport, normaliser, service, &sink,
                           /*rawSink=*/nullptr, /*progressReporter=*/nullptr);

    EXPECT_EQ(stats.linesRead, 1u);
    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_EQ(stats.malformedLines, 0u);
    EXPECT_TRUE(dir.exists("noprogress.csv"));
}

TEST(PipelineReplayTest, NullRawSink_WritesDecodedCsvAndRecordsNoRaw) {
    // Contract: a null raw sink with a non-null decoded sink writes the decoded
    // CSV for a decoded frame and does NOT attempt raw recording (no .raw.txt).
    TempDir dir;
    std::string capture = dir.writeCapture("cap_noraw.csv",
        "1000,118,8,3C00180004A001FF\n");

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    FileTransport transport(capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;

    std::string base = dir.base("noraw");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(transport, normaliser, service, &sink,
                           /*rawSink=*/nullptr);

    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_TRUE(dir.exists("noraw.csv"));
    EXPECT_FALSE(dir.exists("noraw.raw.txt"));
}
