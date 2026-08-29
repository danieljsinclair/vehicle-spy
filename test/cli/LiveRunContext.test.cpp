// LiveRunContext.test.cpp - Entry-point coverage for the LIVE orchestration
// context, the counterpart to ReplayRunContext.test.cpp. LiveRunContext::run
// wires a live transport (here: the bounded, hermetic "demo" transport) through
// LiveTwaiSource + BOTH sinks; the contract under test is that a live run
// writes <base>.raw.txt (the verbatim transport stream — the capture a later
// replay consumes) AND <base>.csv (the decode). No mocks of production code:
// the real run() path drives the real pipeline with a real DBC service.

#include <gtest/gtest.h>
#include "vehicle-sim/cli/LiveRunContext.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;

namespace {

// Minimal RAII temp dir (mirrors the ReplayRunContext test helper).
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("vhsim_livectx_" + std::to_string(counter_++))) {
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

// RAII swap of a process-global stream (std::cout / std::cerr) for a string
// buffer, restored unconditionally (mirrors the TelemetryRunnerStdoutCsv test
// helper). Keeps the 600-frame demo run silent in the test log AND lets the
// tests assert on what run() actually streamed.
class StreamCapture {
public:
    explicit StreamCapture(std::ostream& target)
        : target_(target), original_(target.rdbuf(buffer_.rdbuf())) {}
    ~StreamCapture() { target_.rdbuf(original_); }
    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;
    [[nodiscard]] std::string text() const { return buffer_.str(); }

private:
    // Declaration order matters: buffer_ must be constructed before original_
    // initialises from its rdbuf.
    std::ostream& target_;
    std::ostringstream buffer_;
    std::streambuf* original_;
};

class LiveRunContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<DBCTranslationService>();
        DefaultVehicleConfigs::registerAll(service_->registry());
    }
    std::unique_ptr<DBCTranslationService> service_;
};

} // namespace

// The defining LIVE contract: run() writes BOTH <base>.raw.txt (one
// "<utc_ms>,<verbatim transport line>" row per captured frame) AND <base>.csv.
// The demo transport emits a bounded driving loop (600 frames) then EOF, so
// run() terminates on its own — no signal-based stop needed.
TEST_F(LiveRunContextTest, Run_DemoTarget_WritesRawTxtAndCsv) {
    TempDir dir;
    std::string coutText;
    {
        StreamCapture coutCapture(std::cout);
        StreamCapture cerrCapture(std::cerr);
        int rc = LiveRunContext::run("demo", "tesla", "raw", dir.base("live"),
                                     *service_, /*stdoutCsv=*/true);
        ASSERT_EQ(rc, 0);
        coutText = coutCapture.text();
    }

    ASSERT_TRUE(dir.exists("live.raw.txt"))
        << "live run must write <base>.raw.txt (the capture)";
    ASSERT_TRUE(dir.exists("live.csv"))
        << "live run must write <base>.csv (the decode)";

    // Every raw row is "<epoch-ms>,<line>"; the payload is the verbatim
    // "<CANID> <D0..D7>" text form the demo transport streams.
    std::istringstream raw(dir.read("live.raw.txt"));
    std::string row;
    std::size_t rawRows = 0;
    while (std::getline(raw, row)) {
        if (row.empty()) continue;
        ++rawRows;
        const auto comma = row.find(',');
        ASSERT_NE(comma, std::string::npos) << "raw row must be \"<ts>,<line>\": " << row;
        const auto ts = row.substr(0, comma);
        EXPECT_EQ(ts.find_first_not_of("0123456789"), std::string::npos)
            << "timestamp must be all decimal digits: " << row;
        EXPECT_GT(ts.size(), 10u) << "epoch-ms timestamps are ~13 digits: " << row;
        EXPECT_NE(row.substr(comma + 1).find_first_of(' '), std::string::npos)
            << "payload must be the \"<CANID> <bytes...>\" transport line: " << row;
    }
    EXPECT_GT(rawRows, 0u) << "the demo stream must have been captured";

    // One raw row per decoded CSV data row: the capture is complete relative
    // to the decode (every frame the CSV shows was recorded verbatim).
    std::istringstream csv(dir.read("live.csv"));
    std::string csvLine;
    std::size_t csvDataRows = 0;
    while (std::getline(csv, csvLine)) {
        if (!csvLine.empty()) ++csvDataRows;
    }
    ASSERT_GT(csvDataRows, 1u);  // header + data
    EXPECT_EQ(rawRows, csvDataRows - 1)
        << "raw.txt must carry exactly one verbatim row per decoded frame";

    // stdoutCsv=true keeps stdout a clean CSV pipe (header present).
    EXPECT_NE(coutText.find(','), std::string::npos)
        << "stdout must carry the streamed CSV";
}

// Empty log base = stream-only run: succeeds and writes no artifacts.
TEST_F(LiveRunContextTest, Run_EmptyLogBase_WritesNoFiles) {
    TempDir dir;
    std::string coutText;
    {
        StreamCapture coutCapture(std::cout);
        StreamCapture cerrCapture(std::cerr);
        int rc = LiveRunContext::run("demo", "tesla", "raw", /*logBase=*/"",
                                     *service_, /*stdoutCsv=*/false);
        EXPECT_EQ(rc, 0);
        coutText = coutCapture.text();
    }
    EXPECT_FALSE(dir.exists("live.raw.txt"));
    EXPECT_FALSE(dir.exists("live.csv"));
}

// Unsupported connect target -> run() fails fast (returns 1) before any file
// is touched. Pins the composition-root failure contract.
TEST_F(LiveRunContextTest, Run_UnsupportedTarget_ReturnsOne) {
    TempDir dir;
    std::string cerrText;
    int rc = 0;
    {
        StreamCapture coutCapture(std::cout);
        StreamCapture cerrCapture(std::cerr);
        rc = LiveRunContext::run("bogus-transport", "tesla", "raw",
                                 dir.base("out"), *service_, /*stdoutCsv=*/false);
        cerrText = cerrCapture.text();
    }
    EXPECT_EQ(rc, 1);
    EXPECT_NE(cerrText.find("Unsupported live connect target"), std::string::npos);
    EXPECT_FALSE(dir.exists("out.raw.txt"));
    EXPECT_FALSE(dir.exists("out.csv"));
}
