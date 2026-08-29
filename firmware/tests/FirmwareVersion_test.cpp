// FirmwareVersion_test.cpp - locks the build-identifying version contract
//
// FIRMWARE_BUILD_VERSION is GENERATED per build (scripts/gen_firmware_build_info.sh
// -> vanilla/FirmwareBuildInfo.h, gitignored) from the checked-in semver in
// vanilla/FirmwareVersion.h. The VALUE changes with every commit/build, so
// these tests pin the SHAPE, not a frozen literal: a broken generator (or a
// hand-edited header) fails here instead of shipping an unidentifiable build.
//
// Contract: "<semver>+<short-git-hash>[-dirty] (<build-date>)"
//           e.g. "0.3.0+427d556-dirty (2026-08-29)"
// Fallback (git-less build): "<semver>+unknown (no-vcs)"

#include "FirmwareVersion.h"

#include <cctype>
#include <gtest/gtest.h>
#include <string>

namespace {

bool isLowerHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// Validate "<semver>+<hash>[-dirty] (<ISO-date>)", or the git-less fallback
// "<semver>+unknown (no-vcs)". semver is whatever FIRMWARE_VERSION says (the
// single source of truth the generator composes from).
bool buildVersionIsWellFormed(const std::string& v) {
    const std::string semver = FIRMWARE_VERSION;
    if (v.rfind(semver + "+", 0) != 0) return false;
    std::size_t pos = semver.size() + 1;

    // Git-less fallback: exactly "<semver>+unknown (no-vcs)".
    if (v.compare(pos, 16, "unknown (no-vcs)") == 0 && v.size() == pos + 16) {
        return true;
    }

    // Short git hash: >= 7 lowercase hex chars (git lengthens on ambiguity).
    std::size_t hashLen = 0;
    while (pos + hashLen < v.size() && isLowerHex(v[pos + hashLen])) ++hashLen;
    if (hashLen < 7) return false;
    pos += hashLen;

    // Optional -dirty marker.
    if (v.compare(pos, 6, "-dirty") == 0) pos += 6;

    // " (YYYY-MM-DD)" tail — nothing may follow the closing paren.
    if (pos >= v.size() || v[pos] != ' ') return false;
    ++pos;
    if (pos >= v.size() || v[pos] != '(') return false;
    ++pos;
    const std::size_t close = v.find(')', pos);
    if (close == std::string::npos || close + 1 != v.size()) return false;
    const std::string date = v.substr(pos, close - pos);
    if (date.size() != 10) return false;
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) {
            if (date[i] != '-') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
            return false;
        }
    }
    return true;
}

// The generated value this build actually compiled with is well-formed.
TEST(FirmwareVersionTest, BuildVersionIsWellFormed) {
    EXPECT_TRUE(buildVersionIsWellFormed(FIRMWARE_BUILD_VERSION));
}

// Lock the validator itself on fixed shapes (built from the live semver so
// these never go stale on a version bump): canonical, dirty, and no-vcs
// fallback pass; malformed shapes fail.
TEST(FirmwareVersionTest, ValidatorAcceptsAndRejectsKnownShapes) {
    const std::string s = FIRMWARE_VERSION;
    EXPECT_TRUE(buildVersionIsWellFormed(s + "+427d556 (2026-08-29)"));
    EXPECT_TRUE(buildVersionIsWellFormed(s + "+427d556-dirty (2026-08-29)"));
    EXPECT_TRUE(buildVersionIsWellFormed(s + "+0123456789abcdef (2026-01-02)"));
    EXPECT_TRUE(buildVersionIsWellFormed(s + "+unknown (no-vcs)"));

    EXPECT_FALSE(buildVersionIsWellFormed(s + " (2026-08-29)"));         // no +hash
    EXPECT_FALSE(buildVersionIsWellFormed(s + "+427d5 (2026-08-29)"));   // short hash
    EXPECT_FALSE(buildVersionIsWellFormed(s + "+427D556 (2026-08-29)")); // uppercase hash
    EXPECT_FALSE(buildVersionIsWellFormed(s + "+427d556 (2026-8-29)"));  // non-ISO date
    EXPECT_FALSE(buildVersionIsWellFormed(s + "+427d556 2026-08-29"));   // no parens
    EXPECT_FALSE(buildVersionIsWellFormed(s + "+427d556 (2026-08-29) ")); // trailing junk
    EXPECT_FALSE(buildVersionIsWellFormed("0.0.0+427d556 (2026-08-29)")); // wrong semver
}

// The checked-in semver is a plain MAJOR.MINOR.PATCH (no build metadata —
// that is the generator's job).
TEST(FirmwareVersionTest, SemverIsPlainDottedTriple) {
    const std::string s = FIRMWARE_VERSION;
    ASSERT_FALSE(s.empty());
    std::size_t dots = 0;
    for (const char c : s) {
        if (c == '.') {
            ++dots;
        } else {
            EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(c)))
                << "non-digit '" << c << "' in FIRMWARE_VERSION";
        }
    }
    EXPECT_EQ(dots, 2);
}

} // namespace
