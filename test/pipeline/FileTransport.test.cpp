#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/FileTransport.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace vehicle_sim::pipeline;

namespace {

// RAII temp file — writes content on construction, removes on destruction.
class TempFile {
public:
    explicit TempFile(std::string content)
        : path_((std::filesystem::temp_directory_path() /
                 ("vhsim_fttest_" + std::to_string(counter_++) + ".txt")).string()),
          content_(std::move(content)) {
        std::ofstream out(path_);
        out << content_;
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
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

TEST(FileTransportTest, OpenFailsForMissingFile) {
    FileTransport t("/definitely/does/not/exist/file.csv");
    EXPECT_FALSE(t.open());
    EXPECT_FALSE(t.isOpen());
}

TEST(FileTransportTest, OpenSucceedsForExistingFile) {
    TempFile tmp("hello\nworld\n");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());
    EXPECT_TRUE(t.isOpen());
}

TEST(FileTransportTest, ReadLinesStripsNewline) {
    TempFile tmp("1000,118,8,3C00180004A001FF\n2000,225,3,AABBCC\n");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());

    auto a = t.nextLine();
    auto b = t.nextLine();
    auto c = t.nextLine();

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_FALSE(c.has_value());  // EOF
    EXPECT_EQ(*a, "1000,118,8,3C00180004A001FF");
    EXPECT_EQ(*b, "2000,225,3,AABBCC");
}

TEST(FileTransportTest, ReadPreservesCarriageReturnOnCrlf) {
    // The normaliser — not the transport — decides how to tolerate CRLF. The
    // transport must deliver the line as-is (sans \n) so verbatim captures
    // from CRLF sources decode identically.
    TempFile tmp("1000,118,8,3C00180004A001FF\r\n");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());
    auto a = t.nextLine();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, "1000,118,8,3C00180004A001FF\r");
}

TEST(FileTransportTest, EmptyFileOpensAndReturnsNullopt) {
    TempFile tmp("");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());
    EXPECT_FALSE(t.nextLine().has_value());
}

TEST(FileTransportTest, FileWithTrailingNewlineReadsAllLines) {
    // "a\nb\n" → getline yields "a", "b", then fails (no spurious empty line).
    TempFile tmp("a\nb\n");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());
    EXPECT_EQ(*t.nextLine(), "a");
    EXPECT_EQ(*t.nextLine(), "b");
    EXPECT_FALSE(t.nextLine().has_value());
}

TEST(FileTransportTest, NextLineAfterExhaustionStaysNullopt) {
    TempFile tmp("only\n");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());
    EXPECT_EQ(*t.nextLine(), "only");
    EXPECT_FALSE(t.nextLine().has_value());
    EXPECT_FALSE(t.nextLine().has_value());  // idempotent at EOF
}

TEST(FileTransportTest, OpenIsIdempotent) {
    TempFile tmp("x\n");
    FileTransport t(tmp.path());
    ASSERT_TRUE(t.open());
    EXPECT_TRUE(t.open());  // second open is a no-op, still valid
    EXPECT_EQ(*t.nextLine(), "x");
}
