#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/LiveTwaiSource.h"
#include "vehicle-sim/pipeline/ITransport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace vehicle_sim::pipeline;

namespace {

// Minimal in-memory transport that yields each scripted line once.
class ScriptedTransport final : public ITransport {
public:
    explicit ScriptedTransport(std::vector<std::string> lines)
        : lines_(std::move(lines)) {}

    bool open() override { return true; }
    [[nodiscard]] bool isOpen() const noexcept override { return idx_ < lines_.size(); }
    std::optional<std::string> nextLine() override {
        if (idx_ >= lines_.size()) return std::nullopt;
        return lines_[idx_++];
    }
private:
    std::vector<std::string> lines_;
    std::size_t idx_ = 0;
};

} // namespace

TEST(LiveTwaiSourceTest, RawMode_ParsesSingleFrame) {
    ScriptedTransport t({"118 3C 00 18 00 00 00 00 FF"});
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());
    auto f = src.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[1], 0x01);
    EXPECT_EQ(f->bytes[2], 0x3C);
    EXPECT_EQ(f->bytes[9], 0xFF);
    EXPECT_GT(f->timestampMs, 0u) << "live path must stamp wall-clock on each frame";
    EXPECT_FALSE(src.nextFrame().has_value());
}

TEST(LiveTwaiSourceTest, RawMode_SkipsBlankAndUnparseableLines) {
    ScriptedTransport t({
        "",
        "   \r",
        "# notepad header",
        "ELM327 v2.3",          // not a frame (first token not hex)
        "118 3C 00 18 00 00 00 00 FF",
    });
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());
    // Skip the non-frame lines silently until a real frame appears.
    auto f = src.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[1], 0x01);
    EXPECT_FALSE(src.nextFrame().has_value());
}

TEST(LiveTwaiSourceTest, RawMode_TimestampAdvancesAcrossFrames) {
    // Two frames in a row should both get wall-clock stamps (>= the first).
    ScriptedTransport t({
        "118 3C 00 18 00 00 00 00 FF",
        "225 AA BB CC",
    });
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());
    auto a = src.nextFrame();
    auto b = src.nextFrame();
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_GE(b->timestampMs, a->timestampMs);
}

TEST(LiveTwaiSourceTest, RawMode_RejectsTooManyDataBytes) {
    // 9 data tokens after the CAN-ID is too many → skipped.
    ScriptedTransport t({
        "201 01 02 03 04 05 06 07 08 09",
        "118 3C 00 18 00 00 00 00 FF",
    });
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());
    auto f = src.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[1], 0x01);
}

TEST(LiveTwaiSourceTest, RawMode_RejectsTokenWithMoreThanTwoHexChars) {
    // "ZZZ" isn't hex → line skipped.
    ScriptedTransport t({
        "ZZZ 3C 00 18 00 00 00 00 FF",
        "118 3C 00 18 00 00 00 00 FF",
    });
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());
    auto f = src.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[1], 0x01);
}

TEST(LiveTwaiSourceTest, RawMode_RejectsOutOfRangeByte) {
    // "1FF" parses as 0x1FF (511) which is > 0xFF → line skipped.
    ScriptedTransport t({
        "118 1FF 00 18 00 00 00 00 FF",
        "118 3C 00 18 00 00 00 00 FF",
    });
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());
    auto f = src.nextFrame();
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->bytes[0], 0x18);
    EXPECT_EQ(f->bytes[2], 0x3C);
}
