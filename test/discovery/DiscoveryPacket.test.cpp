#include <gtest/gtest.h>
#include "vehicle-sim/discovery/DiscoveryPacket.h"

#include <cstring>
#include <cstdint>

using namespace vehicle_sim::discovery;

// ── Constants ────────────────────────────────────────────────────────────

TEST(DiscoveryPacketConstants, MagicBytes) {
    ASSERT_EQ(MAGIC.size(), 4u);
    EXPECT_EQ(MAGIC[0], 0x56);
    EXPECT_EQ(MAGIC[1], 0x53);
    EXPECT_EQ(MAGIC[2], 0x49);
    EXPECT_EQ(MAGIC[3], 0x4D);
}

TEST(DiscoveryPacketConstants, HeaderLength) {
    // magic(4) + version(1) + type(1) + deviceId(16) + nonce(8) + timestamp(8) + canPort(2) + otaPort(2) = 42
    EXPECT_EQ(HEADER_LEN, 42u);
}

TEST(DiscoveryPacketConstants, PacketLength) {
    // header(42) + signature(64) = 106
    EXPECT_EQ(PACKET_LEN, 106u);
}

TEST(DiscoveryPacketConstants, DiscoveryPort) {
    EXPECT_EQ(DISCOVERY_PORT, 3335u);
}

TEST(DiscoveryPacketConstants, DefaultPorts) {
    EXPECT_EQ(DEFAULT_CAN_PORT, 3333u);
    EXPECT_EQ(DEFAULT_OTA_PORT, 80u);
}

// ── Serialization ─────────────────────────────────────────────────────────

TEST(DiscoveryPacketSerialize, HeaderContainsMagic) {
    DiscoveryPacket pkt{};
    std::array<uint8_t, DEVICE_ID_LEN> deviceId{};
    deviceId.fill(0xAB);
    std::array<uint8_t, NONCE_LEN> nonce{};
    nonce.fill(0xCD);
    pkt.deviceId = deviceId;
    pkt.nonce = nonce;
    pkt.timestamp = 1700000000;
    pkt.canPort = 3333;
    pkt.otaPort = 3334;
    pkt.signature.fill(0);

    uint8_t buf[HEADER_LEN];
    pkt.serializeHeader(buf);

    EXPECT_EQ(buf[0], 0x56);
    EXPECT_EQ(buf[1], 0x53);
    EXPECT_EQ(buf[2], 0x49);
    EXPECT_EQ(buf[3], 0x4D);
    EXPECT_EQ(buf[4], 1);  // version
    EXPECT_EQ(buf[5], 1);  // packet type
}

TEST(DiscoveryPacketSerialize, HeaderContainsDeviceId) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0);
    pkt.deviceId[0] = 0x01;
    pkt.deviceId[15] = 0x0F;
    pkt.nonce.fill(0);
    pkt.timestamp = 1;
    pkt.canPort = 3333;
    pkt.otaPort = 3334;
    pkt.signature.fill(0);

    uint8_t buf[HEADER_LEN];
    pkt.serializeHeader(buf);

    EXPECT_EQ(buf[6], 0x01);
    EXPECT_EQ(buf[21], 0x0F);
}

TEST(DiscoveryPacketSerialize, HeaderContainsTimestampBigEndian) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0);
    pkt.nonce.fill(0);
    pkt.timestamp = 0x0000000167000000;  // known pattern
    pkt.canPort = 3333;
    pkt.otaPort = 3334;
    pkt.signature.fill(0);

    uint8_t buf[HEADER_LEN];
    pkt.serializeHeader(buf);

    // Big-endian: highest byte first at offset 30
    EXPECT_EQ(buf[30], 0x00);
    EXPECT_EQ(buf[31], 0x00);
    EXPECT_EQ(buf[32], 0x00);
    EXPECT_EQ(buf[33], 0x01);
    EXPECT_EQ(buf[34], 0x67);
}

TEST(DiscoveryPacketSerialize, HeaderContainsCanPortBigEndian) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0);
    pkt.nonce.fill(0);
    pkt.timestamp = 1;
    pkt.canPort = 3333;  // 0x0D05
    pkt.otaPort = 3334;
    pkt.signature.fill(0);

    uint8_t buf[HEADER_LEN];
    pkt.serializeHeader(buf);

    // 3333 = 0x0D05 → big-endian: 0x0D, 0x05
    EXPECT_EQ(buf[38], 0x0D);
    EXPECT_EQ(buf[39], 0x05);
}

TEST(DiscoveryPacketSerialize, HeaderContainsOtaPortBigEndian) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0);
    pkt.nonce.fill(0);
    pkt.timestamp = 1;
    pkt.canPort = 3333;
    pkt.otaPort = 3334;  // 0x0D06
    pkt.signature.fill(0);

    uint8_t buf[HEADER_LEN];
    pkt.serializeHeader(buf);

    // 3334 = 0x0D06 → big-endian: 0x0D, 0x06
    EXPECT_EQ(buf[40], 0x0D);
    EXPECT_EQ(buf[41], 0x06);
}

TEST(DiscoveryPacketSerialize, FullPacketIncludesSignature) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0);
    pkt.nonce.fill(0);
    pkt.timestamp = 1;
    pkt.canPort = 3333;
    pkt.otaPort = 3334;
    pkt.signature.fill(0);
    pkt.signature[0] = 0xDE;
    pkt.signature[63] = 0xAD;

    auto bytes = pkt.toBytes();
    ASSERT_EQ(bytes.size(), PACKET_LEN);

    // Signature starts at offset 42
    EXPECT_EQ(bytes[42], 0xDE);
    EXPECT_EQ(bytes[105], 0xAD);
}

TEST(DiscoveryPacketSerialize, SignedPayloadIsHeaderOnly) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0x11);
    pkt.nonce.fill(0x22);
    pkt.timestamp = 1700000000;
    pkt.canPort = 3333;
    pkt.otaPort = 3334;
    pkt.signature.fill(0x33);

    auto payload = pkt.signedPayload();
    ASSERT_EQ(payload.size(), HEADER_LEN);

    // The signed payload should NOT contain the signature bytes
    for (size_t i = 0; i < HEADER_LEN; ++i) {
        EXPECT_NE(payload[i], 0x33) << "Signature byte leaked into signed payload at " << i;
    }
}

// ── Parsing ───────────────────────────────────────────────────────────────

static std::vector<uint8_t> buildValidPacketData(uint64_t timestamp = 1700000000,
                                                  uint16_t canPort = 3333,
                                                  uint16_t otaPort = 3334) {
    std::vector<uint8_t> data(PACKET_LEN, 0);

    // Magic
    data[0] = 0x56; data[1] = 0x53; data[2] = 0x49; data[3] = 0x4D;
    // Version
    data[4] = 1;
    // Packet type
    data[5] = 1;
    // Device ID (16 bytes) — fill with incrementing values
    for (int i = 0; i < 16; ++i) data[6 + i] = static_cast<uint8_t>(i + 1);
    // Nonce (8 bytes)
    for (int i = 0; i < 8; ++i) data[22 + i] = static_cast<uint8_t>(0xA0 + i);
    // Timestamp (8 bytes, big-endian)
    for (int i = 7; i >= 0; --i) {
        data[30 + i] = static_cast<uint8_t>(timestamp & 0xFF);
        timestamp >>= 8;
    }
    // CAN port (2 bytes, big-endian)
    data[38] = static_cast<uint8_t>((canPort >> 8) & 0xFF);
    data[39] = static_cast<uint8_t>(canPort & 0xFF);
    // OTA port (2 bytes, big-endian)
    data[40] = static_cast<uint8_t>((otaPort >> 8) & 0xFF);
    data[41] = static_cast<uint8_t>(otaPort & 0xFF);
    // Signature (64 bytes) — leave as zeros for parse tests

    return data;
}

TEST(DiscoveryPacketParse, ValidPacket) {
    auto data = buildValidPacketData();
    DiscoveryPacket pkt;
    ASSERT_TRUE(parse(data, pkt));

    // Check device ID
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(pkt.deviceId[i], static_cast<uint8_t>(i + 1));
    }

    // Check nonce
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(pkt.nonce[i], static_cast<uint8_t>(0xA0 + i));
    }

    EXPECT_EQ(pkt.timestamp, 1700000000u);
    EXPECT_EQ(pkt.canPort, 3333u);
    EXPECT_EQ(pkt.otaPort, 3334u);
}

TEST(DiscoveryPacketParse, TooShort) {
    std::vector<uint8_t> data(50, 0);
    DiscoveryPacket pkt;
    EXPECT_FALSE(parse(data, pkt));
}

TEST(DiscoveryPacketParse, WrongMagic) {
    auto data = buildValidPacketData();
    data[0] = 0xFF;
    DiscoveryPacket pkt;
    EXPECT_FALSE(parse(data, pkt));
}

TEST(DiscoveryPacketParse, UnsupportedVersion) {
    auto data = buildValidPacketData();
    data[4] = 99;
    DiscoveryPacket pkt;
    EXPECT_FALSE(parse(data, pkt));
}

TEST(DiscoveryPacketParse, WrongPacketType) {
    auto data = buildValidPacketData();
    data[5] = 2;
    DiscoveryPacket pkt;
    EXPECT_FALSE(parse(data, pkt));
}

TEST(DiscoveryPacketParse, ZeroTimestamp) {
    auto data = buildValidPacketData(0);
    DiscoveryPacket pkt;
    EXPECT_FALSE(parse(data, pkt));
}

TEST(DiscoveryPacketParse, ZeroCanPort) {
    auto data = buildValidPacketData(1700000000, 0);
    DiscoveryPacket pkt;
    EXPECT_FALSE(parse(data, pkt));
}

// ── Round-trip ────────────────────────────────────────────────────────────

TEST(DiscoveryPacketRoundTrip, SerializeThenParse) {
    DiscoveryPacket original{};
    original.deviceId.fill(0);
    original.deviceId[0] = 0x42;
    original.deviceId[15] = 0x88;
    original.nonce.fill(0xAA);
    original.timestamp = 1700000000;
    original.canPort = 3333;
    original.otaPort = 3334;
    original.signature.fill(0xBB);

    auto bytes = original.toBytes();
    DiscoveryPacket parsed;
    ASSERT_TRUE(parse(bytes, parsed));

    EXPECT_EQ(parsed.deviceId, original.deviceId);
    EXPECT_EQ(parsed.nonce, original.nonce);
    EXPECT_EQ(parsed.timestamp, original.timestamp);
    EXPECT_EQ(parsed.canPort, original.canPort);
    EXPECT_EQ(parsed.otaPort, original.otaPort);
    EXPECT_EQ(parsed.signature, original.signature);
}

TEST(DiscoveryPacketRoundTrip, DoubleRoundTrip) {
    DiscoveryPacket original{};
    original.deviceId.fill(0x12);
    original.nonce.fill(0x34);
    original.timestamp = 1672531200;
    original.canPort = 3333;
    original.otaPort = 3334;
    original.signature.fill(0x56);

    auto bytes1 = original.toBytes();
    DiscoveryPacket mid;
    ASSERT_TRUE(parse(bytes1, mid));

    auto bytes2 = mid.toBytes();
    DiscoveryPacket final_;
    ASSERT_TRUE(parse(bytes2, final_));

    EXPECT_EQ(final_.deviceId, original.deviceId);
    EXPECT_EQ(final_.nonce, original.nonce);
    EXPECT_EQ(final_.timestamp, original.timestamp);
    EXPECT_EQ(final_.canPort, original.canPort);
    EXPECT_EQ(final_.otaPort, original.otaPort);
    EXPECT_EQ(final_.signature, original.signature);
}
