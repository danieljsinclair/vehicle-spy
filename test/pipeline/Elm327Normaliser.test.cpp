#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/Elm327Normaliser.h"

#include <array>
#include <cstdint>

using namespace vehicle_sim::pipeline;

// ============================================================
// Elm327Normaliser — parses ELM327 CAN-monitor (ATMA + ATH1) output, i.e.
// lines of the form "<3-hex-ID> <D0> ... <D7>" (e.g. "1D5 29 00 00 00 00 A0 9F")
// whitespace-separated hex, NO timestamp prefix. The normaliser emits a
// TwaiFrame with timestampMs=0; LiveTwaiSource overwrites it with wall-clock
// on the live path.
//
// This is the normaliser the live ELM327 path uses (TCPTransport with
// --adapter-protocol elm327). The AT-init / '>' prompt sequencing is owned
// by boundary::ELM327Transport; this normaliser owns ONLY the per-line
// monitor -> TwaiFrame translation.
// ============================================================

namespace {

std::uint32_t canIdOf(const TwaiFrame& f) {
    return static_cast<std::uint32_t>(f.bytes[0])
         | (static_cast<std::uint32_t>(f.bytes[1]) << 8);
}

} // namespace

TEST(Elm327NormaliserTest, ParsesMonitorEightDataFrame) {
    Elm327Normaliser n;
    auto r = n.normalise("1D5 29 00 00 00 00 00 A0 9F");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.timestampMs, 0u);
    EXPECT_EQ(canIdOf(r.frame), 0x1D5u);
    EXPECT_EQ(r.frame.bytes[2], 0x29);
    EXPECT_EQ(r.frame.bytes[9], 0x9F);
}

TEST(Elm327NormaliserTest, ParsesMonitorFrameWithFullEightDataBytes) {
    Elm327Normaliser n;
    auto r = n.normalise("264 00 00 00 90 01 10 27 00");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x264u);
    EXPECT_EQ(r.frame.bytes[5], 0x90);
    EXPECT_EQ(r.frame.bytes[6], 0x01);
    EXPECT_EQ(r.frame.bytes[7], 0x10);
    EXPECT_EQ(r.frame.bytes[8], 0x27);
    EXPECT_EQ(r.frame.bytes[9], 0x00);
}

TEST(Elm327NormaliserTest, ParsesMonitorFrameWithFewerDataBytes) {
    Elm327Normaliser n;
    auto r = n.normalise("225 AA BB CC");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x225u);
    EXPECT_EQ(r.frame.bytes[2], 0xAA);
    EXPECT_EQ(r.frame.bytes[3], 0xBB);
    EXPECT_EQ(r.frame.bytes[4], 0xCC);
}

TEST(Elm327NormaliserTest, ParsesLowercaseHex) {
    Elm327Normaliser n;
    auto r = n.normalise("1d5 29 00 a0");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x1D5u);
    EXPECT_EQ(r.frame.bytes[4], 0xA0);
}

TEST(Elm327NormaliserTest, ToleratesTrailingCarriageReturn) {
    Elm327Normaliser n;
    auto r = n.normalise("1D5 29 00 00 00 00 00 A0 9F\r");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x1D5u);
}

TEST(Elm327NormaliserTest, ToleratesTrailingNewlineAndSpaces) {
    Elm327Normaliser n;
    auto r = n.normalise("1D5 29 00 00 00 00 00 A0 9F   \r\n");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x1D5u);
}

TEST(Elm327NormaliserTest, ToleratesTabSeparatedTokens) {
    Elm327Normaliser n;
    auto r = n.normalise("1D5\t29\t00\tA0");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x1D5u);
    EXPECT_EQ(r.frame.bytes[4], 0xA0);
}

// ============================================================
// Skip: adapter chatter that is normal monitor-mode noise, not an error
// ============================================================

TEST(Elm327NormaliserTest, ReadyPromptIsSkipped) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise(">").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise(">\r").kind, NormaliserResultKind::Skip);
}

TEST(Elm327NormaliserTest, BlankLineIsSkipped) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("").kind, NormaliserResultKind::Skip);
}

TEST(Elm327NormaliserTest, WhitespaceOnlyLineIsSkipped) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("   \r").kind, NormaliserResultKind::Skip);
}

TEST(Elm327NormaliserTest, StatusStringsAreSkipped) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("NO DATA").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("DATA ERROR").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("STOPPED").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("?").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("OK").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("SEARCHING...").kind, NormaliserResultKind::Skip);
}

TEST(Elm327NormaliserTest, BannerAndVersionStringsAreSkipped) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("ELM327").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("ELM327 v2.3").kind, NormaliserResultKind::Skip);
}

// ============================================================
// Malformed: frame-shaped (starts with hex) but fails to parse
// ============================================================

TEST(Elm327NormaliserTest, MalformedTooManyDataBytes) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("1D5 01 02 03 04 05 06 07 08 09").kind,
              NormaliserResultKind::Malformed);
}

TEST(Elm327NormaliserTest, MalformedNonHexDataToken) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("1D5 29 ZZ 00").kind, NormaliserResultKind::Malformed);
}

TEST(Elm327NormaliserTest, MalformedCanIdAbove11Bits) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("800 00 00 00 00 00 00 00 00").kind,
              NormaliserResultKind::Malformed);
}

TEST(Elm327NormaliserTest, MalformedOversizedIdToken) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("1234 00 00").kind, NormaliserResultKind::Malformed);
}

// ============================================================
// Static parser surface (mirrors the live inline parser test)
// ============================================================

TEST(Elm327NormaliserTest, ParseMonitorLineIsAvailableAsStaticForUnitTesting) {
    auto r = Elm327Normaliser::parseMonitorLine("1D5 29 00 00 00 00 00 A0 9F");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(canIdOf(r.frame), 0x1D5u);
}
