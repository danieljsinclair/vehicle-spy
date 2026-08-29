#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/BinaryFileSource.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace vehicle_sim::pipeline;

namespace {

class TempFile {
public:
    explicit TempFile(std::string content)
        : path_((std::filesystem::temp_directory_path() /
                 ("vhsim_bfs_" + std::to_string(counter_++) + ".raw.txt")).string()),
          content_(std::move(content)) {
        std::ofstream out(path_, std::ios::binary);
        out << content_;
    }
    ~TempFile() { std::error_code ec; std::filesystem::remove(path_, ec); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
private:
    std::string path_;
    std::string content_;
    static int counter_;
};
int TempFile::counter_ = 0;

} // namespace

TEST(BinaryFileSourceTest, OpenFailsForMissingFile) {
    BinaryFileSource s("/definitely/does/not/exist.raw.txt");
    EXPECT_FALSE(s.open());
    EXPECT_FALSE(s.isOpen());
}

TEST(BinaryFileSourceTest, OpenSucceedsForExistingFile) {
    TempFile t("1785964637479,118 3C 00 18 00 00 00 00 FF\n");
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    EXPECT_TRUE(s.isOpen());
}

TEST(BinaryFileSourceTest, AsciiFrame_ProducesTwaiBytes) {
    // CAN ID 0x118 (LE: 0x18 0x01), 8 bytes 0x3C 0x00 0x18 0x00 0x00 0x00 0x00 0xFF
    TempFile t("1785964637479,118 3C 00 18 00 00 00 00 FF\n");
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    auto f = s.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->timestampMs, 1785964637479u);
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[1], 0x01);
    EXPECT_EQ(f->bytes[2], 0x3C);
    EXPECT_EQ(f->bytes[3], 0x00);
    EXPECT_EQ(f->bytes[4], 0x18);
    EXPECT_EQ(f->bytes[5], 0x00);
    EXPECT_EQ(f->bytes[6], 0x00);
    EXPECT_EQ(f->bytes[7], 0x00);
    EXPECT_EQ(f->bytes[8], 0x00);
    EXPECT_EQ(f->bytes[9], 0xFF);
    EXPECT_FALSE(s.nextFrame().has_value());
}

TEST(BinaryFileSourceTest, AsciiFrameWithFewerDataBytes_PadsWithZero) {
    // CAN ID 0x225 with 3 data bytes 0xAA 0xBB 0xCC
    TempFile t("1000,225 AA BB CC\n");
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    auto f = s.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x25);
    EXPECT_EQ(f->bytes[1], 0x02);
    EXPECT_EQ(f->bytes[2], 0xAA);
    EXPECT_EQ(f->bytes[3], 0xBB);
    EXPECT_EQ(f->bytes[4], 0xCC);
    for (std::size_t i = 5; i < 10; ++i) EXPECT_EQ(f->bytes[i], 0u);
}

TEST(BinaryFileSourceTest, BinaryFrame_ProducesTwaiBytes) {
    // 10 raw TWAI bytes after the comma: canId=0x0394 (LE: 0x94 0x03), d0..d7
    std::string content;
    content += "1785964637479,";
    content += std::string("\x94\x03\x11\x22\x33\x44\x55\x66\x77\x88", 10);
    content += "\n";
    TempFile t(content);
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    auto f = s.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->timestampMs, 1785964637479u);
    EXPECT_EQ(f->bytes[0], 0x94);
    EXPECT_EQ(f->bytes[1], 0x03);
    EXPECT_EQ(f->bytes[2], 0x11);
    EXPECT_EQ(f->bytes[3], 0x22);
    EXPECT_EQ(f->bytes[4], 0x33);
    EXPECT_EQ(f->bytes[5], 0x44);
    EXPECT_EQ(f->bytes[6], 0x55);
    EXPECT_EQ(f->bytes[7], 0x66);
    EXPECT_EQ(f->bytes[8], 0x77);
    EXPECT_EQ(f->bytes[9], 0x88);
}

TEST(BinaryFileSourceTest, MultipleAsciiFrames_AllDecoded) {
    TempFile t(
        "1000,118 3C 00 18 00 00 00 00 FF\n"
        "2000,225 AA BB CC\n"
        "3000,3A2 60 E6 72 B1 1D 86 CE 6C\n"
    );
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    int n = 0;
    while (auto f = s.nextFrame()) ++n;
    EXPECT_EQ(n, 3);
}

TEST(BinaryFileSourceTest, MalformedLine_SkippedSilently) {
    // The first line has no comma (no timestamp prefix) — silently skipped.
    TempFile t(
        "no-comma-line\n"
        "1000,118 3C 00 18 00 00 00 00 FF\n"
    );
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    auto f = s.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[1], 0x01);
}

TEST(BinaryFileSourceTest, EmptyFile_YieldsNoFrames) {
    TempFile t("");
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    EXPECT_FALSE(s.nextFrame().has_value());
}

TEST(BinaryFileSourceTest, CranksBrakeLight_OnRealBinaryCapture) {
    // A minimal binary frame for CAN 0x3E2 (994) — the brake light status CAN
    // id per the user-reported overlay. After parsing, the decoder would
    // produce a VehicleSignal with brake light info. This test pins that
    // BinaryFileSource produces the expected 10-byte TWAI shape for it.
    std::string content;
    content += "1785964637479,";
    content += std::string("\xE2\x03\x01\x00\x00\x00\x00\x00\x00\x00", 10);
    content += "\n";
    TempFile t(content);
    BinaryFileSource s(t.path());
    ASSERT_TRUE(s.open());
    auto f = s.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0xE2);
    EXPECT_EQ(f->bytes[1], 0x03);
    EXPECT_EQ(f->bytes[2], 0x01);
}
