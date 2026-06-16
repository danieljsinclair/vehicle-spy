#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"

#include <array>
#include <cstdint>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

// ============================================================
// RawFrameNormaliser — parses LIVE raw adapter output, i.e. lines of the form
//   "<ID> <D0> <D1> ... <D7>"   (e.g. "118 3C 00 18 00 00 00 00")
// whitespace-separated hex, NO timestamp prefix. Live frames are timestamped
// on receipt (timestampMs == 0 here; the decoder stamps wall-clock time).
//
// This is the live-raw normaliser (TCPTransport → RawFrameNormaliser). The
// capture-file forms (legacy CSV + verbatim "ts,ID D0...") are handled by
// CaptureNormaliser; ELM327 AT-init/prompt parsing is a later task (#18).
// ============================================================

TEST(RawFrameNormaliserTest, ParsesLiveEightDataFrame) {
    RawFrameNormaliser n;
    auto r = n.normalise("118 3C 00 18 00 00 00 00 FF");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_FALSE(r.hasTimestamp) << "live-raw frames must not claim to carry timestamps";
    EXPECT_EQ(r.frame.timestampMs, 0u);  // live — stamped on receipt
    EXPECT_EQ(r.frame.canId, 0x118u);
    EXPECT_EQ(r.frame.dlc, 8);
    const std::array<std::uint8_t, 8> expected{0x3C, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0xFF};
    EXPECT_EQ(r.frame.data, expected);
}

TEST(RawFrameNormaliserTest, ParsesLiveFrameWithFewerDataBytes) {
    RawFrameNormaliser n;
    auto r = n.normalise("225 AA BB CC");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x225u);
    EXPECT_EQ(r.frame.dlc, 3);
    EXPECT_EQ(r.frame.data[0], 0xAA);
    EXPECT_EQ(r.frame.data[1], 0xBB);
    EXPECT_EQ(r.frame.data[2], 0xCC);
}

TEST(RawFrameNormaliserTest, ParsesLiveFrameWithZeroDataBytes) {
    // A bare CAN-ID (remote frame) is a valid frame with dlc 0.
    RawFrameNormaliser n;
    auto r = n.normalise("118");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x118u);
    EXPECT_EQ(r.frame.dlc, 0);
}

TEST(RawFrameNormaliserTest, ParsesLiveFrameWithFullEightDataTokens) {
    RawFrameNormaliser n;
    auto r = n.normalise("264 00 00 00 90 01 10 27 00");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x264u);
    EXPECT_EQ(r.frame.dlc, 8);
    EXPECT_EQ(r.frame.data[7], 0x00);
}

TEST(RawFrameNormaliserTest, ToleratesTrailingCarriageReturn) {
    // Live adapters terminate lines with '\r' (ELM327) or '\n'; the transport
    // strips '\n' but may deliver a trailing '\r'. The normaliser must tolerate it.
    RawFrameNormaliser n;
    auto r = n.normalise("118 3C 00 18 00 00 00 00\r");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x118u);
}

TEST(RawFrameNormaliserTest, ToleratesTabSeparatedTokens) {
    RawFrameNormaliser n;
    auto r = n.normalise("118\t3C\t00\t18\t00\t00\t00\t00");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x118u);
    EXPECT_EQ(r.frame.dlc, 7);
}

TEST(RawFrameNormaliserTest, BannerPromptAndStatusTextAreSkipped) {
    // Non-hex first token → not a frame, not an error (Skip). Covers the
    // ELM327 banner "ELM327 v2.3", the '>' prompt, "OK", "NO DATA", etc.
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("ELM327 v2.3").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise(">").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("OK").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("NO DATA").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("STOPPED").kind, NormaliserResultKind::Skip);
    EXPECT_EQ(n.normalise("?").kind, NormaliserResultKind::Skip);
}

TEST(RawFrameNormaliserTest, BlankLineIsSkipped) {
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("").kind, NormaliserResultKind::Skip);
}

TEST(RawFrameNormaliserTest, WhitespaceOnlyLineIsSkipped) {
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("   \r").kind, NormaliserResultKind::Skip);
}

TEST(RawFrameNormaliserTest, HashHeaderIsSkipped) {
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("# timestamp_ms,raw_line").kind, NormaliserResultKind::Skip);
}

TEST(RawFrameNormaliserTest, MalformedTooManyDataTokens) {
    // More than 8 data bytes after the CAN-ID is Malformed (frame-shaped but broken).
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("201 01 02 03 04 05 06 07 08 09").kind,
              NormaliserResultKind::Malformed);
}

TEST(RawFrameNormaliserTest, MalformedNonHexDataToken) {
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("118 3C ZZ 18").kind, NormaliserResultKind::Malformed);
}

TEST(RawFrameNormaliserTest, ParseLiveLineIsAvailableAsStaticForUnitTesting) {
    // The pure parser is exposed statically so the TCP worker and tests can
    // call it without instantiating the normaliser.
    auto r = RawFrameNormaliser::parseLiveLine("118 3C 00 18 00 00 00 00");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x118u);
}

TEST(RawFrameNormaliserTest, DoesNotParseCaptureFileLines) {
    // Capture-file lines ("ts,ID D0..." and "ts,id,dlc,hex") are NOT this
    // normaliser's job. A numeric timestamp token is NOT a hex CAN-ID-friendly
    // first token only when it's followed by a comma; here the comma splits make
    // the first token a pure number which IS hex-parseable, so the line
    // "1000,118,8,3C00180004A001FF" tokenizes as ONE token "1000,118,8,3C00180004A001FF"
    // (no whitespace) → a single non-whitespace token. Since commas aren't hex,
    // isHex("1000,118,...") is false → Skip. This confirms the two normalisers
    // are distinct: file replay uses CaptureNormaliser, not this one.
    RawFrameNormaliser n;
    EXPECT_EQ(n.normalise("1000,118,8,3C00180004A001FF").kind,
              NormaliserResultKind::Skip);
}
