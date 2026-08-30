// ReplayRunContext.test.cpp - Entry-point coverage for the offline replay
// orchestration context. ReplayRunContext::run is a thin static orchestrator
// (BinaryFileSource + DecodedCsvSink + runReplay) over the already-tested
// pipeline pieces; these tests drive the WHOLE run() path off 0% via real
// (hermetic) temp files + a real DBCTranslationService — no mocks of
// production code, no fragile assertions on exact strings.

#include <gtest/gtest.h>
#include "vehicle-sim/cli/ReplayRunContext.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;

namespace {

// Minimal RAII temp dir (mirrors the PipelineReplay test helper). Holds a real
// capture file + output base on the OS temp dir so the test exercises real
// file I/O without touching the source tree.
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("vhsim_replayctx_" + std::to_string(counter_++))) {
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
    [[nodiscard]] std::string read(const std::string& rel) const {
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

// A well-formed legacy CSV capture the tesla DBC can decode.
const char* kDecodableCapture =
    "timestamp_ms,raw_line\n"
    "1000,118 3C 00 18 00 04 A0 01 FF\n"
    "2000,118 3C 00 18 00 04 A0 01 FF\n";

} // namespace

class ReplayRunContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<DBCTranslationService>();
        DefaultVehicleConfigs::registerAll(service_->registry());
    }
    std::unique_ptr<DBCTranslationService> service_;
};

// Happy path: a decodable capture + a valid vehicle + a CSV base -> run returns
// 0 and writes <base>.csv (never <base>.raw.txt — replay's input file is the
// raw source of truth). Drives the full success branch of run().
TEST_F(ReplayRunContextTest, Run_DecodableCapture_WritesCsvAndReturnsZero) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap.csv", kDecodableCapture);

    int rc = ReplayRunContext::run(capture, /*vehicleType=*/"tesla",
                                   /*logBase=*/dir.base("out"), *service_);

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(dir.exists("out.csv"));
    // Replay never writes a raw log (input file is the raw source of truth).
    EXPECT_FALSE(dir.exists("out.raw.txt"));
}

// Empty log base = decode-only run: run() still succeeds (returns 0) and writes
// no decoded CSV. Drives the no-sink branch.
TEST_F(ReplayRunContextTest, Run_EmptyLogBase_SucceedsWithoutCsvOutput) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_nolog.csv", kDecodableCapture);

    int rc = ReplayRunContext::run(capture, "tesla", /*logBase=*/"", *service_);

    EXPECT_EQ(rc, 0);
    EXPECT_FALSE(dir.exists("anything.csv"));
}

// Missing capture file -> run() fails fast (returns 1) before touching the
// decoder. Pins the open()-failure contract.
TEST_F(ReplayRunContextTest, Run_MissingCaptureFile_ReturnsOne) {
    TempDir dir;
    std::string missing = dir.base("does_not_exist.csv");

    int rc = ReplayRunContext::run(missing, "tesla", dir.base("out"), *service_);

    EXPECT_EQ(rc, 1);
}

// Unwritable CSV base (a directory the sink cannot open as a file) -> run()
// returns 1. Pins the decoded-sink-failure contract (open/isValid failure).
TEST_F(ReplayRunContextTest, Run_UnwritableCsvBase_ReturnsOne) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_badout.csv", kDecodableCapture);
    // Use the temp dir path itself as the base — the sink tries to open it as a
    // file ("<dir>.csv") inside a path that is itself a directory's parent; the
    // open must fail because the base resolves to an existing directory entry.
    // Pick a base whose .csv collides with an existing directory.
    std::filesystem::create_directories(dir.base("blocked") + ".csv");

    int rc = ReplayRunContext::run(capture, "tesla", dir.base("blocked"), *service_);

    EXPECT_EQ(rc, 1);
}

// Unknown vehicle: resolveVehicleContext throws std::runtime_error (run() does
// not swallow it). Pin the real propagation contract — the documented "returns
// 1 on unknown vehicle" is inaccurate; the throw is the actual behaviour.
TEST_F(ReplayRunContextTest, Run_UnknownVehicle_PropagatesRuntimeError) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_unknown.csv", kDecodableCapture);

    EXPECT_THROW(
        ReplayRunContext::run(capture, "nonexistent_vehicle", dir.base("x"), *service_),
        std::runtime_error);
}

// ----------------------------------------------------------------------------
// #vs-start-from hint: when stdout-csv replay skips a prefix, ONE comment line
// before the CSV header declares the skip so machine consumers (engine-sim-cli
// --live-telemetry) can keep their display timecode relative to the recording's
// TRUE start instead of restarting at [00:00].
// ----------------------------------------------------------------------------

// RAII std::cout capture — the hint and the CSV rows share the stdout stream.
class CoutCapture {
public:
    CoutCapture() : old_(std::cout.rdbuf(out_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old_); }
    CoutCapture(const CoutCapture&) = delete;
    CoutCapture& operator=(const CoutCapture&) = delete;
    [[nodiscard]] std::string take() const { return out_.str(); }
private:
    std::ostringstream out_;
    std::streambuf* old_;
};

// A capture whose first KEPT row is at rel 0.6s (startFrom=0.5): the first
// kept frame emits immediately (no wall sleep for the skipped prefix), so the
// run is fast even under the paced replay clock.
const char* kSkipCapture =
    "timestamp_ms,raw_line\n"
    "0,118 3C 00 18 00 04 A0 01 FF\n"
    "600,118 3C 00 18 00 04 A0 01 FF\n";

TEST_F(ReplayRunContextTest, Run_StdoutCsvStartFrom_EmitsHintBeforeHeader) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_hint.csv", kSkipCapture);

    CoutCapture captureOut;
    const int rc = ReplayRunContext::run(capture, "tesla", /*logBase=*/"", *service_,
                                         /*stdoutCsv=*/true, /*startFromS=*/0.5);
    ASSERT_EQ(rc, 0);
    std::string out = captureOut.take();

    // First line: the hint. Second line: the CSV header (never the reverse).
    const auto firstNl = out.find('\n');
    ASSERT_NE(firstNl, std::string::npos);
    EXPECT_EQ(out.substr(0, firstNl), "#vs-start-from 0.500");
    const auto secondNl = out.find('\n', firstNl + 1);
    ASSERT_NE(secondNl, std::string::npos);
    EXPECT_EQ(out.substr(firstNl + 1, secondNl - firstNl - 1).substr(0, 12),
              "timestamp_ms")
        << "the CSV header must follow the hint line";
}

TEST_F(ReplayRunContextTest, Run_StdoutCsvNoSkip_EmitsNoHint) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_nohint.csv",
                                           "timestamp_ms,raw_line\n"
                                           "0,118 3C 00 18 00 04 A0 01 FF\n");

    CoutCapture captureOut;
    const int rc = ReplayRunContext::run(capture, "tesla", /*logBase=*/"", *service_,
                                         /*stdoutCsv=*/true, /*startFromS=*/-1.0);
    ASSERT_EQ(rc, 0);
    std::string out = captureOut.take();

    EXPECT_EQ(out.find("#vs-start-from"), std::string::npos)
        << "no skip -> no hint line";
    EXPECT_EQ(out.substr(0, 12), "timestamp_ms") << "stream starts at the header";
}

TEST_F(ReplayRunContextTest, Run_NoStdoutCsv_EmitsNoHintOnStdout) {
    TempDir dir;
    std::string capture = dir.writeCapture("cap_nocsv.csv", kSkipCapture);

    CoutCapture captureOut;
    const int rc = ReplayRunContext::run(capture, "tesla", /*logBase=*/"", *service_,
                                         /*stdoutCsv=*/false, /*startFromS=*/2.0);
    ASSERT_EQ(rc, 0);
    // stdout is the human narrative here; the protocol line must not appear.
    EXPECT_EQ(captureOut.take().find("#vs-start-from"), std::string::npos);
}
