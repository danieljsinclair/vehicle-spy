#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/pipeline/RawLogSink.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/Gear.h"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

namespace {

// RAII temp dir — a unique directory we can drop files into and remove on exit.
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("vhsim_sinktest_" + std::to_string(counter_++))) {
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
// DecodedCsvSink — writes <base>.csv with the existing telemetry schema.
// ============================================================

TEST(DecodedCsvSinkTest, WritesHeaderWithCsvExtension) {
    TempDir dir;
    std::string base = dir.base("out");
    {
        DecodedCsvSink sink(base);
        ASSERT_TRUE(sink.isValid());
    }
    EXPECT_TRUE(dir.exists("out.csv"));
    EXPECT_FALSE(dir.exists("out.raw.txt"));  // decoded sink never writes raw
}

TEST(DecodedCsvSinkTest, HeaderHasExpectedColumns) {
    TempDir dir;
    std::string base = dir.base("hdr");
    {
        DecodedCsvSink sink(base);
    }
    auto content = dir.read("hdr.csv");
    EXPECT_NE(content.find("timestamp_ms"), std::string::npos);
    EXPECT_NE(content.find("speed_kmh"), std::string::npos);
    EXPECT_NE(content.find("throttle_percent"), std::string::npos);
    EXPECT_NE(content.find("gear_selector"), std::string::npos);
    EXPECT_NE(content.find("vehicle_id"), std::string::npos);
    EXPECT_NE(content.find("dbc_signal_count"), std::string::npos);
}

TEST(DecodedCsvSinkTest, WritesOneRowPerSignal) {
    TempDir dir;
    std::string base = dir.base("rows");
    {
        DecodedCsvSink sink(base);
        sink.write(VehicleSignal(1000, 50.0, 80.75, {}, {}, {}, {}, {}, {}, {}, Gear::AUTO_1));
        sink.write(VehicleSignal(2000, 0.0, 0.0, {}, {}, {}, {}, {}, {}, {}, Gear::PARK));
    }
    auto content = dir.read("rows.csv");
    // Header + 2 rows.
    std::size_t newlines = 0;
    for (char c : content) if (c == '\n') ++newlines;
    EXPECT_EQ(newlines, 3u);
}

TEST(DecodedCsvSinkTest, IsMovable) {
    TempDir dir;
    std::string base = dir.base("mv");
    DecodedCsvSink a(base);
    ASSERT_TRUE(a.isValid());
    DecodedCsvSink b(std::move(a));
    EXPECT_TRUE(b.isValid());
    b.write(VehicleSignal(1, {}, 42.0));
    auto content = dir.read("mv.csv");
    EXPECT_NE(content.find("42.00"), std::string::npos);
}

// ============================================================
// RawLogSink — writes <base>.raw.txt with "<utc_ms>,<line>" per row.
// ============================================================

TEST(RawLogSinkTest, WritesRawTxtExtension) {
    TempDir dir;
    std::string base = dir.base("raw1");
    {
        RawLogSink sink(base);
        ASSERT_TRUE(sink.isValid());
        sink.writeLine("118 3C 00 18 00 04 A0 01 FF");
        sink.writeLine("225 AABBCC");
    }
    EXPECT_TRUE(dir.exists("raw1.raw.txt"));
    EXPECT_FALSE(dir.exists("raw1.csv"));  // raw sink never writes decoded
}

TEST(RawLogSinkTest, WritesLinesWithTimestampPrefix) {
    TempDir dir;
    std::string base = dir.base("raw_ts");
    auto before = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    {
        RawLogSink sink(base);
        sink.writeLine("118 3C 00 18 00 04 A0 01 FF");
    }
    auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto content = dir.read("raw_ts.raw.txt");
    // Format: "<utc_ms>,<line>\n"
    auto commaPos = content.find(',');
    EXPECT_NE(commaPos, std::string::npos) << "timestamp-prefix comma must be present";

    auto tsStr = content.substr(0, commaPos);
    std::uint64_t ts = 0;
    EXPECT_TRUE(std::from_chars(tsStr.data(), tsStr.data() + tsStr.size(), ts, 10).ec == std::errc{})
        << "timestamp prefix must be a valid integer, got: " << tsStr;
    EXPECT_GE(ts, static_cast<std::uint64_t>(before)) << "timestamp must be >= time before write";
    EXPECT_LE(ts, static_cast<std::uint64_t>(after)) << "timestamp must be <= time after write";

    auto rest = content.substr(commaPos + 1);
    EXPECT_EQ(rest, "118 3C 00 18 00 04 A0 01 FF\n");
}

TEST(RawLogSinkTest, EachLineGetsItsOwnTimestamp) {
    // Writing two lines should produce two distinct timestamp prefixes (or at
    // minimum two comma-separated timestamped lines).
    TempDir dir;
    std::string base = dir.base("raw_multi");
    {
        RawLogSink sink(base);
        sink.writeLine("line1");
        sink.writeLine("line2");
    }
    auto content = dir.read("raw_multi.raw.txt");
    std::istringstream ss(content);
    std::string row;
    int lineCount = 0;
    while (std::getline(ss, row)) {
        auto commaPos = row.find(',');
        EXPECT_NE(commaPos, std::string::npos) << "each line must have timestamp prefix";
        EXPECT_GT(commaPos, 0u) << "timestamp prefix must be non-empty";
        ++lineCount;
    }
    EXPECT_EQ(lineCount, 2);
}

TEST(RawLogSinkTest, IsMovable) {
    TempDir dir;
    std::string base = dir.base("raw3");
    RawLogSink a(base);
    ASSERT_TRUE(a.isValid());
    RawLogSink b(std::move(a));
    EXPECT_TRUE(b.isValid());
    b.writeLine("moved");
    auto content = dir.read("raw3.raw.txt");
    // Format: "<utc_ms>,moved\n"
    EXPECT_NE(content.find(",moved"), std::string::npos);
    EXPECT_EQ(content.back(), '\n');
}
