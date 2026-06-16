#include <gtest/gtest.h>
#include "vehicle-sim/domain/CaptureLog.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace vehicle_sim::domain;

namespace {

// RAII helper: writes content to a uniquely-named temp file and removes it on destruction.
class TempCaptureFile {
public:
    explicit TempCaptureFile(std::string content)
        : path_((std::filesystem::temp_directory_path() /
                 ("vhsim_captest_" + std::to_string(counter_++) + ".csv")).string()),
          content_(std::move(content)) {
        std::ofstream out(path_);
        out << content_;
    }

    ~TempCaptureFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempCaptureFile(const TempCaptureFile&) = delete;
    TempCaptureFile& operator=(const TempCaptureFile&) = delete;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    std::string content_;
    static int counter_;
};

int TempCaptureFile::counter_ = 0;

} // namespace

// ============================================================
// parseCaptureLine — legacy CSV format ("timestampMs,canId,dlc,dataHex")
// ============================================================

TEST(CaptureLogParseTest, ParsesFullEightByteFrame) {
    auto p = parseCaptureLine("1000,118,8,3C00180004A001FF");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.timestampMs, 1000u);
    EXPECT_EQ(p.frame.canId, 0x118u);
    EXPECT_EQ(p.frame.dlc, 8);
    const std::array<std::uint8_t, 8> expected{0x3C, 0x00, 0x18, 0x00, 0x04, 0xA0, 0x01, 0xFF};
    EXPECT_EQ(p.frame.data, expected);
}

TEST(CaptureLogParseTest, CanIdIsHexadecimal) {
    // can_id is parsed as HEX, not decimal: 07F -> 0x7F == 127
    auto p = parseCaptureLine("1000,07F,8,0102030405060708");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.canId, 0x7Fu);
}

TEST(CaptureLogParseTest, ToleratesTrailingCarriageReturn) {
    auto p = parseCaptureLine("1000,118,8,3C00180004A001FF\r");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.canId, 0x118u);
    EXPECT_EQ(p.frame.dlc, 8);
}

TEST(CaptureLogParseTest, ParsesZeroDlcEmptyData) {
    auto p = parseCaptureLine("1000,07F,0,");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.timestampMs, 1000u);
    EXPECT_EQ(p.frame.canId, 0x7Fu);
    EXPECT_EQ(p.frame.dlc, 0);
    const std::array<std::uint8_t, 8> empty{};
    EXPECT_EQ(p.frame.data, empty);
}

TEST(CaptureLogParseTest, ParsesPartialData) {
    auto p = parseCaptureLine("1000,225,3,AABBCC");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.dlc, 3);
    EXPECT_EQ(p.frame.data[0], 0xAA);
    EXPECT_EQ(p.frame.data[1], 0xBB);
    EXPECT_EQ(p.frame.data[2], 0xCC);
}

TEST(CaptureLogParseTest, HeaderRowIsNotAFrame) {
    // The legacy CSV header is not an error — callers ignore it silently.
    EXPECT_EQ(parseCaptureLine("timestamp_ms,can_id,dlc,data_hex").result,
              CaptureParseResult::NotAFrame);
}

TEST(CaptureLogParseTest, BlankLineIsNotAFrame) {
    EXPECT_EQ(parseCaptureLine("").result, CaptureParseResult::NotAFrame);
}

TEST(CaptureLogParseTest, WhitespaceOnlyLineIsNotAFrame) {
    EXPECT_EQ(parseCaptureLine("   \r").result, CaptureParseResult::NotAFrame);
}

TEST(CaptureLogParseTest, RejectsMalformedTooFewFields) {
    EXPECT_EQ(parseCaptureLine("1000,118,8").result, CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsMalformedTooManyFields) {
    EXPECT_EQ(parseCaptureLine("1000,118,8,DEADBEEF,extra").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsNonNumericTimestamp) {
    EXPECT_EQ(parseCaptureLine("abc,118,8,DEADBEEF").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsBadHexCanId) {
    EXPECT_EQ(parseCaptureLine("1000,GGHH,8,DEADBEEF").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsBadHexData) {
    EXPECT_EQ(parseCaptureLine("1000,118,8,GGHHIIJJKKLLMMNN").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsOddLengthHexData) {
    // 7 hex chars is not a whole number of bytes
    EXPECT_EQ(parseCaptureLine("1000,118,7,0102030405ZZ").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsDlcGreaterThanEight) {
    EXPECT_EQ(parseCaptureLine("1000,118,9,010203040506070809").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, RejectsMoreThanEightDataBytes) {
    // dlc says 8 but there are 9 bytes of hex
    EXPECT_EQ(parseCaptureLine("1000,118,8,010203040506070809").result,
              CaptureParseResult::Malformed);
}

// ============================================================
// parseCaptureLine — verbatim notepad format ("ts,<CANID> <D0> ... <D7>")
// produced by scripts/serial_to_csv.py. Space-separated hex tokens after the
// first comma; builds the same RawFrame the legacy CSV path produces.
// ============================================================

TEST(CaptureLogParseTest, VerbatimFullEightByteFrame) {
    // "1000,118 3C 00 18 00 04 A0 01 FF" must decode identically to the
    // legacy "1000,118,8,3C00180004A001FF" line.
    auto p = parseCaptureLine("1000,118 3C 00 18 00 04 A0 01 FF");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.timestampMs, 1000u);
    EXPECT_EQ(p.frame.canId, 0x118u);
    EXPECT_EQ(p.frame.dlc, 8);
    const std::array<std::uint8_t, 8> expected{0x3C, 0x00, 0x18, 0x00, 0x04, 0xA0, 0x01, 0xFF};
    EXPECT_EQ(p.frame.data, expected);
}

TEST(CaptureLogParseTest, VerbatimEquivalentToLegacyCsv) {
    // The same logical frame in both formats must yield equal RawFrames.
    auto legacy = parseCaptureLine("1000,225,3,AABBCC");
    auto verbatim = parseCaptureLine("1000,225 AA BB CC");
    ASSERT_EQ(legacy.result, CaptureParseResult::Frame);
    ASSERT_EQ(verbatim.result, CaptureParseResult::Frame);
    EXPECT_EQ(legacy.frame, verbatim.frame);
}

TEST(CaptureLogParseTest, VerbatimZeroDlc) {
    auto p = parseCaptureLine("1000,07F");
    ASSERT_EQ(p.result, CaptureParseResult::Frame);
    EXPECT_EQ(p.frame.timestampMs, 1000u);
    EXPECT_EQ(p.frame.canId, 0x7Fu);
    EXPECT_EQ(p.frame.dlc, 0);
    const std::array<std::uint8_t, 8> empty{};
    EXPECT_EQ(p.frame.data, empty);
}

TEST(CaptureLogParseTest, VerbatimRejectsMoreThanEightDataTokens) {
    // 1 CAN-ID + 9 byte tokens = two frames fused — frame-shaped, so Malformed.
    EXPECT_EQ(parseCaptureLine("1000,201 01 02 03 04 05 06 07 08 09").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, VerbatimRejectsNonHexToken) {
    EXPECT_EQ(parseCaptureLine("1000,118 3C ZZ 18").result,
              CaptureParseResult::Malformed);
}

TEST(CaptureLogParseTest, SkipsVerbatimStatusLineNotAnError) {
    // A status line ("ts,TWAI started @ 500kbps") is not a frame and not an
    // error — NotAFrame so the caller ignores it silently.
    EXPECT_EQ(parseCaptureLine("1718400000456,TWAI started @ 500kbps").result,
              CaptureParseResult::NotAFrame);
}

TEST(CaptureLogParseTest, SkipsVerbatimEscapedCorruptLineNotAnError) {
    // Hex-escaped noise line is not a frame; NotAFrame (caller ignores).
    EXPECT_EQ(parseCaptureLine("1718400000789,\\x88\\x88 junk").result,
              CaptureParseResult::NotAFrame);
}

TEST(CaptureLogParseTest, SkipsVerbatimHeaderNotAnError) {
    // The "# timestamp_ms,raw_line" header is not a frame.
    EXPECT_EQ(parseCaptureLine("# timestamp_ms,raw_line").result,
              CaptureParseResult::NotAFrame);
}

// ============================================================
// toTwaiFrame
// ============================================================

TEST(CaptureLogToTwaiTest, ProducesExactlyTenBytes) {
    RawFrame frame;
    frame.canId = 0x118;
    frame.dlc = 8;
    frame.data = {0x3C, 0x00, 0x18, 0x00, 0x04, 0xA0, 0x01, 0xFF};

    auto bytes = toTwaiFrame(frame);
    ASSERT_EQ(bytes.size(), 10u);
}

TEST(CaptureLogToTwaiTest, EncodesCanIdLittleEndian) {
    RawFrame frame;
    frame.canId = 0x0201;
    frame.dlc = 8;

    auto bytes = toTwaiFrame(frame);
    ASSERT_EQ(bytes.size(), 10u);
    EXPECT_EQ(bytes[0], 0x01); // low byte
    EXPECT_EQ(bytes[1], 0x02); // high byte
}

TEST(CaptureLogToTwaiTest, EncodesHighCanId) {
    RawFrame frame;
    frame.canId = 0x7FF; // max standard 11-bit id
    frame.dlc = 8;

    auto bytes = toTwaiFrame(frame);
    EXPECT_EQ(bytes[0], 0xFF);
    EXPECT_EQ(bytes[1], 0x07);
}

TEST(CaptureLogToTwaiTest, CopiesDataIntoBytesTwoThroughNine) {
    RawFrame frame;
    frame.canId = 0x100;
    frame.dlc = 8;
    frame.data = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

    auto bytes = toTwaiFrame(frame);
    ASSERT_EQ(bytes.size(), 10u);
    EXPECT_EQ(bytes[2], 0x10);
    EXPECT_EQ(bytes[3], 0x20);
    EXPECT_EQ(bytes[4], 0x30);
    EXPECT_EQ(bytes[5], 0x40);
    EXPECT_EQ(bytes[6], 0x50);
    EXPECT_EQ(bytes[7], 0x60);
    EXPECT_EQ(bytes[8], 0x70);
    EXPECT_EQ(bytes[9], 0x80);
}

TEST(CaptureLogToTwaiTest, ZeroPadsShortData) {
    RawFrame frame;
    frame.canId = 0x100;
    frame.dlc = 2;
    frame.data = {0xAA, 0xBB, 0, 0, 0, 0, 0, 0};

    auto bytes = toTwaiFrame(frame);
    ASSERT_EQ(bytes.size(), 10u);
    EXPECT_EQ(bytes[2], 0xAA);
    EXPECT_EQ(bytes[3], 0xBB);
    // remaining 6 data bytes are zero
    for (std::size_t i = 4; i < 10; ++i) {
        EXPECT_EQ(bytes[i], 0u) << "byte " << i;
    }
}

// ============================================================
// loadCaptureFile
// ============================================================

TEST(CaptureLogLoadTest, LoadsValidFramesAndSkipsHeaderAndBlanks) {
    TempCaptureFile tmp(
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
        "\n"                                  // blank -> ignored (not skipped)
        "2000,225,3,AABBCC\n"
        "timestamp_ms,can_id,dlc,data_hex\n"  // repeated header -> ignored
        "\r\n"                                // CRLF blank -> ignored
    );

    auto cap = loadCaptureFile(tmp.path());
    ASSERT_TRUE(cap.opened);
    ASSERT_EQ(cap.frames.size(), 2u);
    EXPECT_EQ(cap.skippedLines, 0u);
    EXPECT_EQ(cap.frames[0].timestampMs, 1000u);
    EXPECT_EQ(cap.frames[0].canId, 0x118u);
    EXPECT_EQ(cap.frames[1].timestampMs, 2000u);
    EXPECT_EQ(cap.frames[1].canId, 0x225u);
    EXPECT_EQ(cap.frames[1].dlc, 3);
}

TEST(CaptureLogLoadTest, CountsSkippedMalformedLines) {
    TempCaptureFile tmp(
        "timestamp_ms,can_id,dlc,data_hex\n"
        "1000,118,8,3C00180004A001FF\n"
        "not,a,valid,line\n"     // 4 fields but bad values -> skipped
        "999,GG,8,00112233\n"    // bad hex can_id -> skipped
        "2000,225,3,AABBCC\n"
    );

    auto cap = loadCaptureFile(tmp.path());
    ASSERT_TRUE(cap.opened);
    ASSERT_EQ(cap.frames.size(), 2u);
    EXPECT_EQ(cap.skippedLines, 2u);
}

TEST(CaptureLogLoadTest, LoadsMixedLegacyAndVerbatimAndSkipsNonFrames) {
    // A notepad capture mixes legacy CSV rows, verbatim rows, and non-frame
    // lines (header, status, escaped noise). Non-frames are skipped silently
    // (not counted as malformed) so a notepad capture decodes cleanly.
    TempCaptureFile tmp(
        "# timestamp_ms,raw_line\n"           // notepad header -> skipped
        "1000,118 3C 00 18 00 04 A0 01 FF\n"  // verbatim frame
        "1000,TWAI started @ 500kbps\n"       // status -> skipped
        "1000,\\x88\\x88 junk\n"              // escaped noise -> skipped
        "2000,225,3,AABBCC\n"                 // legacy CSV frame
    );

    auto cap = loadCaptureFile(tmp.path());
    ASSERT_TRUE(cap.opened);
    ASSERT_EQ(cap.frames.size(), 2u);
    EXPECT_EQ(cap.skippedLines, 0u);  // non-frame lines are not "malformed"
    EXPECT_EQ(cap.frames[0].canId, 0x118u);
    EXPECT_EQ(cap.frames[0].dlc, 8);
    EXPECT_EQ(cap.frames[1].canId, 0x225u);
    EXPECT_EQ(cap.frames[1].dlc, 3);
}

TEST(CaptureLogLoadTest, MissingFileReturnsNotOpened) {
    auto cap = loadCaptureFile("/definitely/does/not/exist/capture.csv");
    EXPECT_FALSE(cap.opened);
    EXPECT_TRUE(cap.frames.empty());
    EXPECT_EQ(cap.skippedLines, 0u);
}

TEST(CaptureLogLoadTest, EmptyFileOpensWithNoFrames) {
    TempCaptureFile tmp("");
    auto cap = loadCaptureFile(tmp.path());
    EXPECT_TRUE(cap.opened);
    EXPECT_TRUE(cap.frames.empty());
    EXPECT_EQ(cap.skippedLines, 0u);
}

TEST(CaptureLogLoadTest, HeaderOnlyFileOpensWithNoFrames) {
    TempCaptureFile tmp("timestamp_ms,can_id,dlc,data_hex\n");
    auto cap = loadCaptureFile(tmp.path());
    EXPECT_TRUE(cap.opened);
    EXPECT_TRUE(cap.frames.empty());
    EXPECT_EQ(cap.skippedLines, 0u);
}
