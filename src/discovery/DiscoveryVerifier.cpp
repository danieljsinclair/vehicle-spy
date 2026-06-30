#include "vehicle-sim/discovery/DiscoveryVerifier.h"

#include <sodium.h>
#include <array>
#include <fstream>
#include <cstring>

namespace vehicle_sim::discovery {

bool loadPublicKey(const std::string& path,
                   std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read raw 32 bytes
    std::array<char, ED25519_PUBLIC_KEY_LEN> buf;
    file.read(buf.data(), ED25519_PUBLIC_KEY_LEN);
    if (file.gcount() != ED25519_PUBLIC_KEY_LEN) {
        return false;
    }

    std::copy(buf.begin(), buf.end(), out.begin());
    return true;
}

bool verifySignature(const DiscoveryPacket& packet,
                     const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& publicKey) {
    // Get the signed payload (header bytes, 42 bytes)
    auto payload = packet.signedPayload();

    // libsodium crypto_sign_ed25519_verify:
    //   Verifies a detached signature. Returns 0 on success.
    //   Note: We use pure Ed25519 here (not Ed25519ph like OTA), since the
    //   payload is only 42 bytes — no need for pre-hashing.
    int result = crypto_sign_ed25519_verify_detached(
        packet.signature.data(),
        payload.data(),
        payload.size(),
        publicKey.data()
    );

    return (result == 0);
}

bool isTimestampFresh(uint64_t packetTimestamp, uint64_t nowEpoch, uint64_t maxClockSkew) {
    // Anti-replay: reject packets with timestamp too far from local clock.
    // Handle potential overflow for future timestamps carefully.
    if (packetTimestamp > nowEpoch) {
        uint64_t diff = packetTimestamp - nowEpoch;
        return diff <= maxClockSkew;
    } else {
        uint64_t diff = nowEpoch - packetTimestamp;
        return diff <= maxClockSkew;
    }
}

bool verify(const DiscoveryPacket& packet,
            const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& publicKey,
            uint64_t nowEpoch,
            uint64_t maxClockSkew) {
    if (!isTimestampFresh(packet.timestamp, nowEpoch, maxClockSkew)) {
        return false;
    }
    return verifySignature(packet, publicKey);
}

} // namespace vehicle_sim::discovery
