#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "vehicle-sim/cli/LogSanitizer.h"

using namespace vehicle_sim::cli;

// Control bytes must not be allowed to reach a LOG sink verbatim — a crafted
// SSID/path/address containing CR/LF could forge extra log lines.
TEST(LogSanitizerTest, ControlBytesBecomeQuestionMark) {
    std::string input;
    input.push_back('\n');       // 0x0A — newline (log-line forging)
    input.push_back('\r');       // 0x0D — carriage return
    input.push_back('\t');       // 0x09 — tab
    input.push_back(0x01);       // 0x01 — SOH
    input.push_back(0x7F);       // 0x7F — DEL
    input.push_back(0x1F);       // 0x1F — just below the 0x20 boundary

    const std::string out = forLog(input);
    EXPECT_EQ(out, "??????");
}

// Every printable byte — including the spaces and punctuation that are
// legitimate in a WiFi password — must pass through byte-identical. The
// sanitizer is ONLY for human-diagnostic sinks; it must not mangle real data.
TEST(LogSanitizerTest, PrintableAsciiIsByteIdentical) {
    const std::string password = "p@ss word!";
    EXPECT_EQ(forLog(password), password);

    const std::string mixed = "abcXYZ0129 .,-_/:?=+";
    EXPECT_EQ(forLog(mixed), mixed);
}

// A crafted value straddling the boundary must be split per-byte: printable
// neighbours survive, only the control byte is substituted.
TEST(LogSanitizerTest, BoundaryByteIsolated) {
    std::string input = "AB";
    input.push_back('\n');
    input += "CD";
    EXPECT_EQ(forLog(input), "AB?CD");
}

TEST(LogSanitizerTest, EmptyInputYieldsEmptyOutput) {
    EXPECT_EQ(forLog(""), "");
    EXPECT_EQ(forLog(std::string_view{}), "");
}

// string_view input (no null terminator) must be handled by length, not by
// scanning for a terminator — the provisioning path passes views of buffers.
TEST(LogSanitizerTest, StringViewLengthIsRespected) {
    const char buf[] = {'a', 'b', '\0', 'c'};
    const std::string_view view(buf, sizeof(buf));
    EXPECT_EQ(forLog(view), "ab?c");
}
