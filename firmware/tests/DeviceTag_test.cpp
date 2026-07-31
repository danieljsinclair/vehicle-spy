// DeviceTag_test.cpp - Host tests for formatDeviceTag
// Extracted from can-bridge.ino for host testability

#include "DeviceTag.h"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <string>

using esp32_firmware::formatDeviceTag;

namespace {

// Helper: build a 16-byte device ID with specific first 4 bytes.
std::array<uint8_t, 16> makeDeviceId(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    std::array<uint8_t, 16> id{};
    id[0] = b0;
    id[1] = b1;
    id[2] = b2;
    id[3] = b3;
    return id;
}

} // namespace

// Happy path: four non-zero bytes produce a [XXXX] tag.
TEST(DeviceTagTest, FormatsNonZeroBytesAsHexTag) {
    auto id = makeDeviceId(0xDE, 0xAD, 0xBE, 0xEF);
    EXPECT_EQ(formatDeviceTag(id), "[DEADBEEF] ");
}

// Contract: leading zeros are preserved (fixed-width %02X).
TEST(DeviceTagTest, PreservesLeadingZeros) {
    auto id = makeDeviceId(0x00, 0x01, 0x0A, 0x00);
    EXPECT_EQ(formatDeviceTag(id), "[00010A00] ");
}

// Contract: all-FF bytes produce [FFFFFFFF] tag.
TEST(DeviceTagTest, FormatsAllOnes) {
    auto id = makeDeviceId(0xFF, 0xFF, 0xFF, 0xFF);
    EXPECT_EQ(formatDeviceTag(id), "[FFFFFFFF] ");
}

// Contract: all-zero bytes produce [00000000] tag.
TEST(DeviceTagTest, FormatsAllZeros) {
    auto id = makeDeviceId(0x00, 0x00, 0x00, 0x00);
    EXPECT_EQ(formatDeviceTag(id), "[00000000] ");
}

// Contract: bytes 4–15 are ignored (only first 4 bytes are tagged).
TEST(DeviceTagTest, IgnoresTrailingBytes) {
    std::array<uint8_t, 16> id = makeDeviceId(0x12, 0x34, 0x56, 0x78);
    id[4] = 0xFF;
    id[5] = 0xFE;
    id[15] = 0x00;
    EXPECT_EQ(formatDeviceTag(id), "[12345678] ");
}

// Contract: mixed case — some zeros, some non-zero — formats correctly.
TEST(DeviceTagTest, FormatsMixedBytes) {
    auto id = makeDeviceId(0x00, 0xFF, 0x7F, 0x80);
    EXPECT_EQ(formatDeviceTag(id), "[00FF7F80] ");
}
