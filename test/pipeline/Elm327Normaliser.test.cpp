#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/Elm327Normaliser.h"

#include <array>
#include <cstdint>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

// ============================================================
// Elm327Normaliser — parses ELM327 CAN-monitor (ATMA + ATH1) output, i.e.
// lines of the form "<3-hex-ID> <D0> ... <D7>" (e.g. "1D5 29 00 00 00 00 A0 9F")
// whitespace-separated hex, NO timestamp prefix. Live monitor frames are
// timestamped on receipt (timestampMs == 0 here; the decoder stamps wall-clock).
//
// This is the normaliser the live ELM327 path uses (TCPTransport with
// --adapter-protocol elm327). The AT-init / '>' prompt sequencing is owned by
// boundary::ELM327Transport (covered by ELM327Transport.test.cpp); this
// normaliser owns ONLY the per-line monitor -> RawFrame translation.
// ============================================================

TEST(Elm327NormaliserTest, ParsesMonitorEightDataFrame) {
    // Mirrors captures/FirstDrive_*.csv row "1D5,8,290000000000A09F" — the
    // 16-hex-digit payload is 8 bytes: 29 00 00 00 00 00 A0 9F.
    Elm327Normaliser n;
    auto r = n.normalise("1D5 29 00 00 00 00 00 A0 9F");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.timestampMs, 0u);  // live — stamped on receipt
    EXPECT_EQ(r.frame.canId, 0x1D5u);
    EXPECT_EQ(r.frame.dlc, 8);
    const std::array<std::uint8_t, 8> expected{0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x9F};
    EXPECT_EQ(r.frame.data, expected);
}

TEST(Elm327NormaliserTest, ParsesMonitorFrameWithFullEightDataBytes) {
    // Full 8-byte payload (the common Tesla monitor shape).
    Elm327Normaliser n;
    auto r = n.normalise("264 00 00 00 90 01 10 27 00");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x264u);
    EXPECT_EQ(r.frame.dlc, 8);
    EXPECT_EQ(r.frame.data[7], 0x00);
}

TEST(Elm327NormaliserTest, ParsesMonitorFrameWithFewerDataBytes) {
    // Fewer than 8 data bytes parses the bytes present (dlc tracks the count).
    Elm327Normaliser n;
    auto r = n.normalise("225 AA BB CC");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x225u);
    EXPECT_EQ(r.frame.dlc, 3);
    EXPECT_EQ(r.frame.data[0], 0xAA);
    EXPECT_EQ(r.frame.data[1], 0xBB);
    EXPECT_EQ(r.frame.data[2], 0xCC);
}

TEST(Elm327NormaliserTest, ParsesLowercaseHex) {
    Elm327Normaliser n;
    auto r = n.normalise("1d5 29 00 a0");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x1D5u);
    EXPECT_EQ(r.frame.dlc, 3);
    EXPECT_EQ(r.frame.data[2], 0xA0);
}

TEST(Elm327NormaliserTest, ToleratesTrailingCarriageReturn) {
    // ELM327 terminates lines with '\r'; the transport may deliver it intact.
    Elm327Normaliser n;
    auto r = n.normalise("1D5 29 00 00 00 00 00 A0 9F\r");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x1D5u);
}

TEST(Elm327NormaliserTest, ToleratesTrailingNewlineAndSpaces) {
    Elm327Normaliser n;
    auto r = n.normalise("1D5 29 00 00 00 00 00 A0 9F   \r\n");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x1D5u);
}

TEST(Elm327NormaliserTest, ToleratesTabSeparatedTokens) {
    Elm327Normaliser n;
    auto r = n.normalise("1D5\t29\t00\tA0");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x1D5u);
    EXPECT_EQ(r.frame.dlc, 3);
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
    // These are normal ELM327 monitor-mode noise — not frames, not errors.
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
    // More than 8 data bytes after the CAN-ID is Malformed.
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("1D5 01 02 03 04 05 06 07 08 09").kind,
              NormaliserResultKind::Malformed);
}

TEST(Elm327NormaliserTest, MalformedNonHexDataToken) {
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("1D5 29 ZZ 00").kind, NormaliserResultKind::Malformed);
}

TEST(Elm327NormaliserTest, MalformedCanIdAbove11Bits) {
    // 0x800 is beyond the 11-bit standard CAN ID range (max 0x7FF).
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("800 00 00 00 00 00 00 00 00").kind,
              NormaliserResultKind::Malformed);
}

TEST(Elm327NormaliserTest, MalformedOversizedIdToken) {
    // A 4+-digit ID token is not an 11-bit monitor ID.
    Elm327Normaliser n;
    EXPECT_EQ(n.normalise("1234 00 00").kind, NormaliserResultKind::Malformed);
}

// ============================================================
// Static parser surface (mirrors RawFrameNormaliser's test)
// ============================================================

TEST(Elm327NormaliserTest, ParseMonitorLineIsAvailableAsStaticForUnitTesting) {
    auto r = Elm327Normaliser::parseMonitorLine("1D5 29 00 00 00 00 00 A0 9F");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x1D5u);
}
