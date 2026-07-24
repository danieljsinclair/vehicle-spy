#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/ITransportOutput.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace vehicle_sim::pipeline;

namespace {

// Capturing sink: records every out()/err() string so a test can assert the
// EXACT bytes TaggedOutput emitted to its base. TaggedOutput's whole job is
// formatting (prefixing/tagging), so asserting the rendered string is the
// direct contract — not vanity, that IS the behavior under test.
class CapturingOutput final : public ITransportOutput {
public:
    void out(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        outLines_.push_back(msg);
    }
    void err(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        errLines_.push_back(msg);
    }

    std::vector<std::string> outLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return outLines_;
    }
    std::vector<std::string> errLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return errLines_;
    }

private:
    mutable std::mutex mu_;
    std::vector<std::string> outLines_;
    std::vector<std::string> errLines_;
};

} // namespace

// ============================================================
// TaggedOutput — formatting contracts
//
// TaggedOutput wraps a base ITransportOutput and formats device-specific
// console messages. These tests pin the rendered prefixes ([CLIENT → ...],
// [ESP32 ...]) and the neutral out()/err() pass-through. Pure formatting
// logic — no hardware, no I/O — so assertions are on the exact emitted string.
// ============================================================

// Contract: outClient() with a populated deviceId emits the
// "[CLIENT → <first8>]" prefix before the message. The first 8 chars of the
// 32-hex device id are used for display.
TEST(TaggedOutputTest, OutClient_WithDeviceId_PrefixesShortId) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "0123456789abcdef0123456789abcdef");

    tagged.outClient("hello");

    const auto lines = base->outLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "[CLIENT → 01234567] hello")
        << "prefix uses first 8 chars of device id";
}

// Contract: outClient() with an EMPTY deviceId emits the message untagged
// (no "[CLIENT → ]" prefix with an empty id).
TEST(TaggedOutputTest, OutClient_WithEmptyDeviceId_EmitsUntagged) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "");

    tagged.outClient("hello");

    const auto lines = base->outLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "hello");
}

// Contract: outDevice() with a populated deviceId wraps the message in the
// blue "[ESP32 <first8>]" prefix (ANSI color codes around it).
TEST(TaggedOutputTest, OutDevice_WithDeviceId_PrefixesBlueEsp32Tag) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "aabbccddeeff00112233445566778899");

    tagged.outDevice("booting");

    const auto lines = base->outLines();
    ASSERT_EQ(lines.size(), 1u);
    // The short id (first 8) and the message must both appear; the blue ANSI
    // escape wraps the tag. Assert on the load-bearing substrings, not the
    // raw escape bytes (color codes are presentation detail).
    EXPECT_NE(lines[0].find("aabbccdd"), std::string::npos) << "missing short id";
    EXPECT_NE(lines[0].find("[ESP32 "), std::string::npos) << "missing ESP32 tag";
    EXPECT_NE(lines[0].find("booting"), std::string::npos) << "missing message";
}

// Contract: outDevice() with an EMPTY deviceId emits the message untagged.
TEST(TaggedOutputTest, OutDevice_WithEmptyDeviceId_EmitsUntagged) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "");

    tagged.outDevice("booting");

    const auto lines = base->outLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "booting");
}

// Contract: out() is the NEUTRAL path — it forwards to base_->out() WITHOUT
// any [CLIENT]/[ESP32] tag, even when a deviceId is set. Callers wanting the
// tag must call outClient()/outDevice() explicitly (the L4 contract-erosion
// fix documented in the source).
TEST(TaggedOutputTest, Out_IsNeutral_ForwardsUntaggedEvenWithDeviceId) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "0123456789abcdef0123456789abcdef");

    tagged.out("plain");

    const auto lines = base->outLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "plain") << "out() must not tag — neutral by contract";
}

// Contract: err() forwards to base_->err() untagged (errors are system-level,
// never device-tagged).
TEST(TaggedOutputTest, Err_ForwardsUntaggedToBaseErr) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "0123456789abcdef0123456789abcdef");

    tagged.err("oops");

    const auto outLines = base->outLines();
    const auto errLines = base->errLines();
    EXPECT_TRUE(outLines.empty()) << "err() must not touch out()";
    ASSERT_EQ(errLines.size(), 1u);
    EXPECT_EQ(errLines[0], "oops");
}

// Contract: setDeviceId() updates the id used by subsequent outClient()/
// outDevice() calls — a TaggedOutput's device binding is mutable over its
// lifetime (used when the connected device changes).
TEST(TaggedOutputTest, SetDeviceId_UpdatesPrefixForSubsequentCalls) {
    auto base = std::make_shared<CapturingOutput>();
    TaggedOutput tagged(base, "");

    // Initially empty → untagged.
    tagged.outClient("first");
    // Bind a device → subsequent call is tagged.
    tagged.setDeviceId("ffffffff00000000ffffffff00000000");
    tagged.outClient("second");

    const auto lines = base->outLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "first");
    EXPECT_EQ(lines[1], "[CLIENT → ffffffff] second");
}
