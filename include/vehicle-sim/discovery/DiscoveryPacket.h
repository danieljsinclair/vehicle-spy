// DiscoveryPacket.h — shared packet format for ESP32 auto-discovery.
//
// THIS IS THE SINGLE SOURCE OF TRUTH for the discovery wire format.
// Both the C++ CLI (src/discovery/) and iOS (DiscoveryPacket.swift) MUST
// use these exact constants and layout. If you change anything here, update
// both sides.
//
// Wire format (big-endian, packed):
//   offset  size  field
//   0       4     magic: "VSIM" (0x56 0x53 0x49 0x4D)
//   4       1     version (currently 1)
//   5       1     packet type (1 = discovery broadcast)
//   6       16    device ID
//   22      8     nonce (anti-replay random bytes)
//   30      8     timestamp (Unix epoch seconds, big-endian uint64)
//   38      2     CAN TCP port (big-endian uint16)
//   40      2     OTA port (big-endian uint16)
//   42      64    Ed25519 signature over bytes 0..41
//   TOTAL: 106 bytes
//
// The Ed25519 signature covers the 42-byte header (everything before the
// signature). The signing key is the same Ed25519 keypair used for OTA.

#ifndef VEHICLE_SIM_DISCOVERY_PACKET_H
#define VEHICLE_SIM_DISCOVERY_PACKET_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <vector>
#include <string_view>

namespace vehicle_sim::discovery {

// ── Constants (single source of truth) ───────────────────────────────────

// Magic bytes: "VSIM"
inline constexpr std::array<uint8_t, 4> MAGIC = {{0x56, 0x53, 0x49, 0x4D}};

inline constexpr uint8_t VERSION = 1;
inline constexpr uint8_t PACKET_TYPE_DISCOVERY = 1;

inline constexpr size_t DEVICE_ID_LEN = 16;
inline constexpr size_t NONCE_LEN = 8;
inline constexpr size_t SIGNATURE_LEN = 64;  // Ed25519

// Header wire format (42 bytes total): 4-byte magic, 1-byte version, 1-byte
// type, 16-byte deviceId, 8-byte nonce, 8-byte timestamp, then 2-byte canPort
// and 2-byte otaPort.
inline constexpr size_t HEADER_LEN = 42;

// A complete packet is the 42-byte header followed by the 64-byte Ed25519
// signature (106 bytes total).
inline constexpr size_t PACKET_LEN = HEADER_LEN + SIGNATURE_LEN;  // 106

// UDP port for discovery broadcasts — ESP32 sends, CLI/iOS listen
inline constexpr uint16_t DISCOVERY_PORT = 3335;

// Default ports
inline constexpr uint16_t DEFAULT_CAN_PORT = 3333;
inline constexpr uint16_t DEFAULT_OTA_PORT = 80;

// ── Packet structure ─────────────────────────────────────────────────────

struct DiscoveryPacket {
    std::array<uint8_t, DEVICE_ID_LEN> deviceId;
    std::array<uint8_t, NONCE_LEN> nonce;
    uint64_t timestamp;   // Unix epoch seconds
    uint16_t canPort;     // TCP port for CAN bridge
    uint16_t otaPort;     // TCP port for OTA
    std::array<uint8_t, SIGNATURE_LEN> signature;

    // Serialize the header (unsigned portion, bytes 0..41) into `out`.
    // `out` must have space for at least HEADER_LEN bytes.
    void serializeHeader(uint8_t* out) const;

    // Serialize the full packet (header + signature) into `out`.
    // `out` must have space for at least PACKET_LEN bytes.
    void serialize(uint8_t* out) const;

    // Return the signed payload (header bytes) as a vector.
    std::vector<uint8_t> signedPayload() const;

    // Return the full wire-format packet as a vector.
    std::vector<uint8_t> toBytes() const;
};

// Parse a discovery packet from raw UDP data.
// Returns true on success, false on failure (malformed).
bool parse(const uint8_t* data, size_t len, DiscoveryPacket& out);

// Parse from a byte vector.
bool parse(const std::vector<uint8_t>& data, DiscoveryPacket& out);

} // namespace vehicle_sim::discovery

#endif // VEHICLE_SIM_DISCOVERY_PACKET_H
