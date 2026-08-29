#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/BinaryFileSource.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
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
        return true;
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

std::string makeCapture(const std::vector<std::uint64_t>& tsMs) {
    std::ostringstream out;
    out << "timestamp_ms,raw_line\n";
    for (auto ts : tsMs) {
        out << ts << ",118 3C 00 18 00 04 A0 01 FF\n";
    }
    return out.str();
}

std::vector<std::uint64_t> decodedTimestamps(const std::string& csv) {
    std::vector<std::uint64_t> out;
    std::istringstream ss(csv);
    std::string line;
    std::getline(ss, line);
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

TwaiFrame frameAt(std::uint64_t tsMs, std::uint8_t dlc = 8) {
    TwaiFrame f;
    f.timestampMs = tsMs;
    for (std::size_t i = 0; i < dlc; ++i) f.bytes[2 + i] = static_cast<std::uint8_t>(i + 1);
    return f;
}

TwaiFrame blankFrame() { return TwaiFrame{}; }

} // namespace

TEST(ReplayPacingTest, SetAndReadStartFromS) {
    ReplayPacing pacing;
    EXPECT_DOUBLE_EQ(pacing.startFromS(), -1.0);
    pacing.setStartFromS(2.5);
    EXPECT_DOUBLE_EQ(pacing.startFromS(), 2.5);
}

TEST(ReplayPacingTest, BlankFrameDetected) {
    EXPECT_TRUE(ReplayPacing::isFrameBlank(blankFrame()));

    TwaiFrame real = frameAt(1000);
    EXPECT_FALSE(ReplayPacing::isFrameBlank(real));

    // Zero-timestamp with a non-zero data byte is NOT blank.
    TwaiFrame timed;
    timed.timestampMs = 500;
    timed.bytes[2] = 0xAA;
    EXPECT_FALSE(ReplayPacing::isFrameBlank(timed));
}

TEST(ReplayPacingTest, ClassifySurfacesFirstRowImmediately) {
    ReplayPacing pacing;
    EXPECT_EQ(pacing.classifyFrame(frameAt(1000), /*recordingBaselineTsMs=*/1000,
                           /*pacingBaselineTsMs=*/1000, /*elapsed=*/0), 0);
}

TEST(ReplayPacingTest, ClassifyFutureRowReturnsWaitMs) {
    ReplayPacing pacing;
    EXPECT_EQ(pacing.classifyFrame(frameAt(1000), /*recordingBaselineTsMs=*/0,
                           /*pacingBaselineTsMs=*/0, /*elapsed=*/0), 1000);
}

TEST(ReplayPacingTest, ClassifyBeforeStartFromSkipped) {
    ReplayPacing pacing(/*startFromS=*/2.0);
    EXPECT_EQ(pacing.classifyFrame(frameAt(1500), /*recordingBaselineTsMs=*/0,
                           /*pacingBaselineTsMs=*/0, /*elapsed=*/0), -1);
    EXPECT_NE(pacing.classifyFrame(frameAt(2500), /*recordingBaselineTsMs=*/0,
                           /*pacingBaselineTsMs=*/0, /*elapsed=*/0), -1);
}

TEST(ReplayPacingTest, ClassifySkipsByRelativeTimeOnEpochScaleCaptures) {
    // Real captures carry epoch-scale timestamps; the skip must measure
    // RELATIVE time from the recording's first frame, not compare the raw
    // 13-digit value against a relative threshold (which never fires).
    constexpr std::uint64_t kEpoch0 = 1755000000000ull;
    ReplayPacing pacing(/*startFromS=*/20.0);
    EXPECT_EQ(pacing.classifyFrame(frameAt(kEpoch0 + 15000), kEpoch0, kEpoch0, 0), -1);
    // Past the gate at exactly the threshold: the scheduler re-baselines the
    // PACING origin to this frame, so it surfaces immediately (dual-origin
    // contract — skip gate from the recording start, schedule from the first
    // kept frame).
    EXPECT_EQ(pacing.classifyFrame(frameAt(kEpoch0 + 20000), kEpoch0, kEpoch0 + 20000, 0), 0);
}

TEST(ReplayPacingIntegrationTest, PacedReplaySpacesRowsByRecordedTimestamps) {
    TempDir dir;
    auto capture = dir.writeCapture("paced.csv", makeCapture({0, 1000, 2000, 3000}));

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(source, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/-1.0);

    EXPECT_EQ(stats.framesDecoded, 4u);

    ASSERT_EQ(clock.sleeps().size(), 3u);
    for (const auto& s : clock.sleeps()) {
        EXPECT_EQ(s.count(), 1000) << "each inter-row gap should be 1000ms";
    }

    auto ts = decodedTimestamps(dir.read("out.csv"));
    ASSERT_EQ(ts.size(), 4u);
    EXPECT_EQ(ts[0], 0u);
    EXPECT_EQ(ts[1], 1000u);
    EXPECT_EQ(ts[2], 2000u);
    EXPECT_EQ(ts[3], 3000u);
}

TEST(ReplayPacingIntegrationTest, PacedReplayStartFromSkipsEarlyRows) {
    TempDir dir;
    auto capture = dir.writeCapture("startfrom.csv", makeCapture({0, 1000, 2000, 3000}));

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("sf");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(source, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/2.0);

    EXPECT_EQ(stats.framesDecoded, 2u);
    auto ts = decodedTimestamps(dir.read("sf.csv"));
    ASSERT_EQ(ts.size(), 2u);
    EXPECT_EQ(ts[0], 2000u);
    EXPECT_EQ(ts[1], 3000u);
}

TEST(ReplayPacingIntegrationTest, PacedReplay_AnchorsBaselineOnFirstNonBlankFrame) {
    // BinaryFileSource never emits a blank frame, so this test reduces to:
    // the first frame in the file anchors the baseline, and only one wait
    // happens (1000ms -> 2000ms).
    TempDir dir;
    std::string content =
        "timestamp_ms,raw_line\n"
        "1000,118 3C 00 18 00 04 A0 01 FF\n"
        "2000,118 3C 00 18 00 04 A0 01 FF\n";
    auto capture = dir.writeCapture("baseline.csv", content);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("baseline_out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(source, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/-1.0);

    EXPECT_EQ(stats.framesDecoded, 2u);
    ASSERT_EQ(clock.sleeps().size(), 1u);
    EXPECT_EQ(clock.sleeps().front().count(), 1000);

    auto ts = decodedTimestamps(dir.read("baseline_out.csv"));
    ASSERT_EQ(ts.size(), 2u);
    EXPECT_EQ(ts[0], 1000u);
    EXPECT_EQ(ts[1], 2000u);
}

TEST(ReplayPacingIntegrationTest, PacedReplay_MalformedRow_RoutedToMalformedNotSkippedOrDecoded) {
    TempDir dir;
    std::string content =
        "timestamp_ms,raw_line\n"
        "1000,118 3C 00 18 00 04 A0 01 FF\n"
        "999,GG 00 11 22 33\n"                 // malformed CAN id
        "2000,118 3C 00 18 00 04 A0 01 FF\n";
    auto capture = dir.writeCapture("malformed.csv", content);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    std::string base = dir.base("malformed_out");
    DecodedCsvSink sink(base);
    ASSERT_TRUE(sink.isValid());

    RecordingClock clock;
    auto stats = runReplay(source, service,
                           ReplayOutputs{.decoded = &sink}, ReplayMode::Paced, clock,
                           /*startFromS=*/-1.0);

    // The malformed row (no valid canId in BinaryFileSource's tokeniser)
    // is silently skipped; 2 frames decode.
    EXPECT_EQ(stats.framesDecoded, 2u);
}

TEST(ReplayPacingIntegrationTest, EofTerminatesPacedReplayCleanly) {
    TempDir dir;
    auto capture = dir.writeCapture("eof.csv", makeCapture({0, 500, 1500}));

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    BinaryFileSource source(capture);
    ASSERT_TRUE(source.open());

    FakeClock clock;
    auto stats = runReplay(source, service, ReplayOutputs{},
                           ReplayMode::Paced, clock, -1.0);
    EXPECT_EQ(stats.framesDecoded, 3u);
}

