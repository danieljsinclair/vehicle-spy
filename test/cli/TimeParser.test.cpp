#include <gtest/gtest.h>
#include "vehicle-sim/cli/TimeParser.h"
#include "vehicle-sim/cli/CliOptions.h"

#include <string>
#include <vector>

using vehicle_sim::cli::parseTimecodeToSeconds;

// ============================================================
// parseTimecodeToSeconds — the --start-from grammar.
//
// These tests exist because the parser once lived as an untracked
// working-copy stub that returned 0.0 for every input: every --start-from
// timecode silently meant "start from the beginning", and a 1362-green
// suite never noticed because nothing pinned the grammar. Every case below
// is a behavior the CLI (and engine-sim-cli's identical grammar) promises.
// ============================================================

TEST(TimeParserTest, PlainSeconds) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("0"), 0.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("90"), 90.0);
}

TEST(TimeParserTest, PlainSecondsFractional) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("90.5"), 90.5);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("0.25"), 0.25);
}

TEST(TimeParserTest, MinutesSeconds) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("01:30"), 90.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("0:05"), 5.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("10:00"), 600.0);
}

TEST(TimeParserTest, MinutesSecondsFractional) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("01:30.5"), 90.5);
}

TEST(TimeParserTest, HoursMinutesSeconds) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1:00:00"), 3600.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("02:03:04"), 2.0 * 3600.0 + 3.0 * 60.0 + 4.0);
}

TEST(TimeParserTest, HoursMinutesSecondsFractional) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1:00:00.5"), 3600.5);
}

TEST(TimeParserTest, EmptyIsInvalid) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds(""), -1.0);
}

TEST(TimeParserTest, StrayColonsAreInvalid) {
    // Leading / trailing colons must not silently parse as a shorter form.
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds(":"), -1.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("01:"), -1.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds(":30"), -1.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1::30"), -1.0)
        << "empty middle field must be invalid, not 0";
}

TEST(TimeParserTest, GarbageIsInvalid) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("abc"), -1.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1:xx:00"), -1.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("start"), -1.0);
}

TEST(TimeParserTest, MoreThanThreeFieldsIsInvalid) {
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1:02:03:04"), -1.0);
}

TEST(TimeParserTest, OutOfDoubleRangeIsInvalid) {
    // std::stod throws out_of_range for values beyond double — the parser
    // must surface invalid, not crash.
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1e999"), -1.0);
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("1e999:00:00"), -1.0);
}

TEST(TimeParserTest, NegativeSecondsParseNegative_CallersTreatAsUnset) {
    // The parser itself mirrors std::stod and returns the negative value;
    // the CLI's "< 0.0 means unset/invalid" check rejects it afterwards.
    EXPECT_DOUBLE_EQ(parseTimecodeToSeconds("-5"), -5.0);
}

// The --start-from wiring consumes the parser through the real option path:
// a valid mm:ss timecode must reach start_from_s, and garbage must become a
// user-facing parse error.
TEST(TimeParserTest, StartFromOption_ParsesTimecodeThroughRealPath) {
    std::vector<std::string> strings{"vehicle-sim", "--start-from", "01:30"};
    std::vector<char*> ptrs;
    for (auto& s : strings) ptrs.push_back(s.data());
    auto opts = vehicle_sim::cli::parseArgs(static_cast<int>(ptrs.size()), ptrs.data());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_DOUBLE_EQ(opts.telemetry.start_from_s, 90.0);
}

TEST(TimeParserTest, StartFromOption_GarbageIsParseErrorThroughRealPath) {
    std::vector<std::string> strings{"vehicle-sim", "--start-from", "not-a-time"};
    std::vector<char*> ptrs;
    for (auto& s : strings) ptrs.push_back(s.data());
    auto opts = vehicle_sim::cli::parseArgs(static_cast<int>(ptrs.size()), ptrs.data());

    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("Invalid --start-from"), std::string::npos);
    EXPECT_DOUBLE_EQ(opts.telemetry.start_from_s, -1.0);
}
