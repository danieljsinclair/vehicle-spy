// DiscoveryVerifier.h — Ed25519 signature verification for discovery packets.
//
// Uses libsodium's crypto_sign_ed25519_verify (pure Ed25519, NOT pre-hashed).
// The ESP32 firmware signs the 42-byte header with crypto_sign_ed25519, and
// the CLI/iOS verify with crypto_sign_ed25519_verify.
//
// The verification public key is the SAME key used for OTA (OtaPublicKey.h on
// the firmware side). For the CLI, it's loaded from the OTA keys directory.

#ifndef VEHICLE_SIM_DISCOVERY_VERIFIER_H
#define VEHICLE_SIM_DISCOVERY_VERIFIER_H

#include "vehicle-sim/discovery/DiscoveryPacket.h"
#include <cstdint>
#include <array>
#include <string>

namespace vehicle_sim {
namespace discovery {

inline constexpr size_t ED25519_PUBLIC_KEY_LEN = 32;
// Default clock skew for timestamp validation: 5 minutes (reasonable for devices with NTP)
inline constexpr uint64_t DEFAULT_MAX_CLOCK_SKEW = 300;  // seconds

// Load the Ed25519 public key from a raw 32-byte file (or the OTA keys dir).
// Returns true on success.
bool loadPublicKey(const std::string& path,
                  std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& out);

// Verify the Ed25519 signature on a discovery packet.
// Returns true if the signature is valid.
bool verifySignature(const DiscoveryPacket& packet,
                    const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& publicKey);

// Check if the packet's timestamp is within `maxClockSkew` seconds of
// `nowEpoch` (Unix epoch seconds).
bool isTimestampFresh(uint64_t packetTimestamp,
                      uint64_t nowEpoch,
                      uint64_t maxClockSkew = DEFAULT_MAX_CLOCK_SKEW);

// Full verification: signature + timestamp freshness.
// Returns true only if both checks pass.
bool verify(const DiscoveryPacket& packet,
            const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& publicKey,
            uint64_t nowEpoch,
            uint64_t maxClockSkew = DEFAULT_MAX_CLOCK_SKEW);

} // namespace discovery
} // namespace vehicle_sim

#endif // VEHICLE_SIM_DISCOVERY_VERIFIER_H
