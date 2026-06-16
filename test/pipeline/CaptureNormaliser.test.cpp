#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/CaptureNormaliser.h"

#include <array>
#include <cstdint>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

// ============================================================
// CaptureNormaliser — parses CAPTURE-FILE lines into RawFrames, reusing
// domain::parseCaptureLine. Supports both capture forms:
//   - verbatim notepad:  "timestamp_ms,<CANID> <D0> ... <D7>"
//   - legacy CSV:        "timestamp_ms,can_id,dlc,data_hex"
// (This is the renamed/moved Phase-1 RawFrameNormaliser; live-raw adapter
// output without a timestamp is handled by RawFrameNormaliser.)
// ============================================================

TEST(CaptureNormaliserTest, ParsesLegacyCsvFullFrame) {
    CaptureNormaliser n;
    auto r = n.normalise("1000,118,8,3C00180004A001FF");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_TRUE(r.hasTimestamp) << "capture-file frames must carry timestamps";
    EXPECT_EQ(r.frame.timestampMs, 1000u);
    EXPECT_EQ(r.frame.canId, 0x118u);
    EXPECT_EQ(r.frame.dlc, 8);
    const std::array<std::uint8_t, 8> expected{0x3C, 0x00, 0x18, 0x00, 0x04, 0xA0, 0x01, 0xFF};
    EXPECT_EQ(r.frame.data, expected);
}

TEST(CaptureNormaliserTest, ParsesVerbatimNotepadFrame) {
    CaptureNormaliser n;
    auto r = n.normalise("1000,118 3C 00 18 00 04 A0 01 FF");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x118u);
    EXPECT_EQ(r.frame.dlc, 8);
}

TEST(CaptureNormaliserTest, VerbatimAndLegacyYieldEqualFrames) {
    CaptureNormaliser n;
    auto legacy = n.normalise("1000,225,3,AABBCC");
    auto verbatim = n.normalise("1000,225 AA BB CC");
    ASSERT_EQ(legacy.kind, NormaliserResultKind::Frame);
    ASSERT_EQ(verbatim.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(legacy.frame, verbatim.frame);
}

TEST(CaptureNormaliserTest, HeaderRowIsSkipped) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("timestamp_ms,can_id,dlc,data_hex").kind,
              NormaliserResultKind::Skip);
}

TEST(CaptureNormaliserTest, NotepadHashHeaderIsSkipped) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("# timestamp_ms,raw_line").kind, NormaliserResultKind::Skip);
}

TEST(CaptureNormaliserTest, BlankLineIsSkipped) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("").kind, NormaliserResultKind::Skip);
}

TEST(CaptureNormaliserTest, WhitespaceOnlyLineIsSkipped) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("   \r").kind, NormaliserResultKind::Skip);
}

TEST(CaptureNormaliserTest, StatusTextIsSkipped) {
    CaptureNormaliser n;
    // Status line: hex-escaped noise after a numeric timestamp → NotAFrame.
    EXPECT_EQ(n.normalise("1718400000789,\\x88\\x88 junk").kind,
              NormaliserResultKind::Skip);
}

TEST(CaptureNormaliserTest, MalformedTooFewFields) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("1000,118,8").kind, NormaliserResultKind::Malformed);
}

TEST(CaptureNormaliserTest, MalformedNonNumericTimestamp) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("abc,118,8,DEADBEEF").kind, NormaliserResultKind::Malformed);
}

TEST(CaptureNormaliserTest, MalformedBadHexCanId) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("1000,GGHH,8,DEADBEEF").kind, NormaliserResultKind::Malformed);
}

TEST(CaptureNormaliserTest, MalformedBadHexData) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("1000,118,8,GGHHIIJJKKLLMMNN").kind,
              NormaliserResultKind::Malformed);
}

TEST(CaptureNormaliserTest, MalformedVerbatimNonHexToken) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("1000,118 3C ZZ 18").kind, NormaliserResultKind::Malformed);
}

TEST(CaptureNormaliserTest, MalformedVerbatimTooManyDataTokens) {
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("1000,201 01 02 03 04 05 06 07 08 09").kind,
              NormaliserResultKind::Malformed);
}

TEST(CaptureNormaliserTest, ToleratesTrailingCarriageReturn) {
    CaptureNormaliser n;
    auto r = n.normalise("1000,118,8,3C00180004A001FF\r");
    ASSERT_EQ(r.kind, NormaliserResultKind::Frame);
    EXPECT_EQ(r.frame.canId, 0x118u);
}

TEST(CaptureNormaliserTest, DoesNotParseLiveRawLine) {
    // A bare live-raw line "118 3C 00 ..." has NO comma, so
    // domain::parseCaptureLine treats it as NotAFrame (Skip). This confirms
    // CaptureNormaliser and RawFrameNormaliser are distinct: live sources use
    // RawFrameNormaliser.
    CaptureNormaliser n;
    EXPECT_EQ(n.normalise("118 3C 00 18 00 04 A0 01 FF").kind,
              NormaliserResultKind::Skip);
}
