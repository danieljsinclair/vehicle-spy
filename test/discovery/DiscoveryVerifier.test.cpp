// DiscoveryVerifier.test.cpp — Tests for Ed25519 signature verification.
//
// Uses real Ed25519 keypairs generated at test time via libsodium. No external
// files or live data. Tests cover: signature creation/verification, timestamp
// freshness, full verify() pipeline, and rejection of tampered packets.

#include <gtest/gtest.h>
#include "vehicle-sim/discovery/DiscoveryVerifier.h"
#include "vehicle-sim/discovery/DiscoveryPacket.h"

#include <sodium.h>
#include <cstdint>
#include <cstring>

using namespace vehicle_sim::discovery;

// ── Fixture: generates a fresh Ed25519 keypair for each test ──────────────

class DiscoveryVerifierTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_GE(sodium_init(), 0);

        // Generate a real Ed25519 keypair
        pk_.fill(0);
        sk_.fill(0);
        ASSERT_EQ(crypto_sign_ed25519_keypair(pk_.data(), sk_.data()), 0);
    }

    // Sign the 42-byte header and store the detached signature in outSignature.
    void signHeader(const uint8_t* header, size_t headerLen,
                    std::array<uint8_t, SIGNATURE_LEN>& outSignature) {
        ASSERT_EQ(crypto_sign_ed25519_detached(
            outSignature.data(), nullptr,
            header, headerLen,
            sk_.data()), 0);
    }

    // Build a valid discovery packet, sign it, and return it.
    DiscoveryPacket makeSignedPacket(uint64_t timestamp = 1700000000,
                                     uint16_t canPort = 3333,
                                     uint16_t otaPort = 3334) {
        DiscoveryPacket pkt{};
        pkt.deviceId.fill(0xAB);
        pkt.nonce.fill(0xCD);
        pkt.timestamp = timestamp;
        pkt.canPort = canPort;
        pkt.otaPort = otaPort;
        pkt.signature.fill(0);

        // Serialize header and sign it
        std::array<uint8_t, HEADER_LEN> header{};
        pkt.serializeHeader(header.data());
        signHeader(header.data(), header.size(), pkt.signature);

        return pkt;
    }

    std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> pk_;
    std::array<uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> sk_;
};

// ── Signature verification ────────────────────────────────────────────────

TEST_F(DiscoveryVerifierTest, ValidSignature) {
    auto pkt = makeSignedPacket();
    EXPECT_TRUE(verifySignature(pkt, pk_));
}

TEST_F(DiscoveryVerifierTest, WrongPublicKey_Rejected) {
    auto pkt = makeSignedPacket();

    // Generate a DIFFERENT keypair
    std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> wrongPk;
    std::array<uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> wrongSk;
    ASSERT_EQ(crypto_sign_ed25519_keypair(wrongPk.data(), wrongSk.data()), 0);

    // Verification with wrong public key must fail
    EXPECT_FALSE(verifySignature(pkt, wrongPk));
}

TEST_F(DiscoveryVerifierTest, TamperedDeviceId_Rejected) {
    auto pkt = makeSignedPacket();

    // Tamper with the device ID after signing
    pkt.deviceId[0] ^= 0xFF;

    // The signature was over the original header, so it must fail
    EXPECT_FALSE(verifySignature(pkt, pk_));
}

TEST_F(DiscoveryVerifierTest, TamperedTimestamp_Rejected) {
    auto pkt = makeSignedPacket();

    // Tamper with the timestamp after signing
    pkt.timestamp = 9999999999;

    EXPECT_FALSE(verifySignature(pkt, pk_));
}

TEST_F(DiscoveryVerifierTest, TamperedCanPort_Rejected) {
    auto pkt = makeSignedPacket();

    // Tamper with the CAN port after signing
    pkt.canPort = 9999;

    EXPECT_FALSE(verifySignature(pkt, pk_));
}

TEST_F(DiscoveryVerifierTest, AllZerosSignature_Rejected) {
    DiscoveryPacket pkt{};
    pkt.deviceId.fill(0x11);
    pkt.nonce.fill(0x22);
    pkt.timestamp = 1700000000;
    pkt.canPort = 3333;
    pkt.otaPort = 3334;
    pkt.signature.fill(0);  // not signed

    EXPECT_FALSE(verifySignature(pkt, pk_));
}

TEST_F(DiscoveryVerifierTest, RandomSignature_Rejected) {
    auto pkt = makeSignedPacket();

    // Replace signature with random bytes
    pkt.signature.fill(0);
    randombytes_buf(pkt.signature.data(), pkt.signature.size());

    EXPECT_FALSE(verifySignature(pkt, pk_));
}

// ── Timestamp freshness ────────────────────────────────────────────────────

TEST_F(DiscoveryVerifierTest, FreshTimestamp) {
    uint64_t now = 1700000000;
    EXPECT_TRUE(isTimestampFresh(now, now, DEFAULT_MAX_CLOCK_SKEW));
    EXPECT_TRUE(isTimestampFresh(now - 100, now, DEFAULT_MAX_CLOCK_SKEW));
    EXPECT_TRUE(isTimestampFresh(now + 100, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, StaleTimestamp) {
    uint64_t now = 1700000000;
    // 301 seconds ago with 300s max skew → stale
    EXPECT_FALSE(isTimestampFresh(now - 301, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, FutureTimestamp_WithinSkew) {
    uint64_t now = 1700000000;
    // 299 seconds in the future with 300s max skew → fresh
    EXPECT_TRUE(isTimestampFresh(now + 299, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, FutureTimestamp_ExceedsSkew) {
    uint64_t now = 1700000000;
    // 301 seconds in the future with 300s max skew → stale
    EXPECT_FALSE(isTimestampFresh(now + 301, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, ExactSkewBoundary) {
    uint64_t now = 1700000000;
    // Exactly 300 seconds ago → fresh (<=)
    EXPECT_TRUE(isTimestampFresh(now - 300, now, DEFAULT_MAX_CLOCK_SKEW));
    // Exactly 300 seconds in the future → fresh (<=)
    EXPECT_TRUE(isTimestampFresh(now + 300, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, CustomSkew) {
    uint64_t now = 1700000000;
    // 60 seconds ago with 30s max skew → stale
    EXPECT_FALSE(isTimestampFresh(now - 60, now, 30));
    // 30 seconds ago with 30s max skew → fresh
    EXPECT_TRUE(isTimestampFresh(now - 30, now, 30));
}

// ── Full verify() pipeline ─────────────────────────────────────────────────

TEST_F(DiscoveryVerifierTest, FullVerify_Success) {
    uint64_t now = 1700000000;
    auto pkt = makeSignedPacket(now);
    EXPECT_TRUE(verify(pkt, pk_, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, FullVerify_StaleTimestamp) {
    uint64_t now = 1700000000;
    auto pkt = makeSignedPacket(now);
    // Verify with a clock that's 600s ahead → timestamp is stale
    EXPECT_FALSE(verify(pkt, pk_, now + 600, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, FullVerify_BadSignature) {
    uint64_t now = 1700000000;
    auto pkt = makeSignedPacket(now);

    // Tamper with the packet
    pkt.deviceId[0] ^= 0x01;

    EXPECT_FALSE(verify(pkt, pk_, now, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, FullVerify_WrongKeyAndStale) {
    uint64_t now = 1700000000;
    auto pkt = makeSignedPacket(now);

    std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> wrongPk;
    std::array<uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> wrongSk;
    ASSERT_EQ(crypto_sign_ed25519_keypair(wrongPk.data(), wrongSk.data()), 0);

    // Both wrong key AND stale → must fail
    EXPECT_FALSE(verify(pkt, wrongPk, now + 600, DEFAULT_MAX_CLOCK_SKEW));
}

// ── Round-trip: sign → verify with real crypto ─────────────────────────────

TEST_F(DiscoveryVerifierTest, RoundTrip_SignParseVerify) {
    // Build a packet, sign it, serialize to bytes, parse back, verify
    auto original = makeSignedPacket(1700000000, 3333, 3334);

    // Serialize
    auto bytes = original.toBytes();
    ASSERT_EQ(bytes.size(), PACKET_LEN);

    // Parse
    DiscoveryPacket parsed;
    ASSERT_TRUE(parse(bytes, parsed));

    // Verify the parsed packet
    EXPECT_TRUE(verifySignature(parsed, pk_));
    EXPECT_TRUE(verify(parsed, pk_, 1700000000, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, RoundTrip_DifferentTimestamps) {
    // Verify that different timestamps produce different valid signatures
    auto pkt1 = makeSignedPacket(1700000000);
    auto pkt2 = makeSignedPacket(1700000001);

    // Both must verify
    EXPECT_TRUE(verifySignature(pkt1, pk_));
    EXPECT_TRUE(verifySignature(pkt2, pk_));

    // But the signatures must differ (different message)
    EXPECT_NE(pkt1.signature, pkt2.signature);
}

// ── Edge cases ──────────────────────────────────────────────────────────────

TEST_F(DiscoveryVerifierTest, MaxTimestamp) {
    uint64_t maxTs = UINT64_MAX - 1;
    auto pkt = makeSignedPacket(maxTs);
    EXPECT_TRUE(verifySignature(pkt, pk_));
    // Timestamp freshness: maxTs is far in the future from any reasonable now
    EXPECT_FALSE(isTimestampFresh(maxTs, 1700000000, DEFAULT_MAX_CLOCK_SKEW));
}

TEST_F(DiscoveryVerifierTest, MinNonZeroTimestamp) {
    auto pkt = makeSignedPacket(1);  // 1 second after epoch
    EXPECT_TRUE(verifySignature(pkt, pk_));
    // 1 is far in the past from 1700000000
    EXPECT_FALSE(isTimestampFresh(1, 1700000000, DEFAULT_MAX_CLOCK_SKEW));
}
