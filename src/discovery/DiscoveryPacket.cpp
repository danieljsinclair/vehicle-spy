#include "vehicle-sim/discovery/DiscoveryPacket.h"
#include <cstring>
#include <algorithm>

namespace vehicle_sim::discovery {

static void writeBigEndian(uint8_t* dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[1] = static_cast<uint8_t>(value & 0xFF);
}

static void writeBigEndian(uint8_t* dst, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        dst[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }
}

static uint16_t readBigEndian16(const uint8_t* src) {
    return static_cast<uint16_t>((static_cast<uint16_t>(src[0]) << 8) | static_cast<uint16_t>(src[1]));
}

static uint64_t readBigEndian64(const uint8_t* src) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint64_t>(src[i]);
    }
    return value;
}

void DiscoveryPacket::serializeHeader(uint8_t* out) const {
    // magic (4 bytes)
    std::copy(MAGIC.begin(), MAGIC.end(), out);
    // version (1 byte)
    out[4] = VERSION;
    // packet type (1 byte)
    out[5] = PACKET_TYPE_DISCOVERY;
    // deviceId (16 bytes)
    std::copy(deviceId.begin(), deviceId.end(), out + 6);
    // nonce (8 bytes)
    std::copy(nonce.begin(), nonce.end(), out + 22);
    // timestamp (8 bytes, big-endian)
    writeBigEndian(out + 30, timestamp);
    // canPort (2 bytes, big-endian)
    writeBigEndian(out + 38, canPort);
    // otaPort (2 bytes, big-endian)
    writeBigEndian(out + 40, otaPort);
}

void DiscoveryPacket::serialize(uint8_t* out) const {
    serializeHeader(out);
    // signature (64 bytes)
    std::copy(signature.begin(), signature.end(), out + HEADER_LEN);
}

std::vector<uint8_t> DiscoveryPacket::signedPayload() const {
    std::vector<uint8_t> payload(HEADER_LEN);
    serializeHeader(payload.data());
    return payload;
}

std::vector<uint8_t> DiscoveryPacket::toBytes() const {
    std::vector<uint8_t> bytes(PACKET_LEN);
    serialize(bytes.data());
    return bytes;
}

bool parse(const uint8_t* data, size_t len, DiscoveryPacket& out) {
    if (len < PACKET_LEN) {
        return false;
    }

    // Check magic
    for (size_t i = 0; i < MAGIC.size(); ++i) {
        if (data[i] != MAGIC[i]) {
            return false;
        }
    }

    // Check version
    if (data[4] != VERSION) {
        return false;
    }

    // Check packet type
    if (data[5] != PACKET_TYPE_DISCOVERY) {
        return false;
    }

    // Device ID
    std::copy(data + 6, data + 6 + DEVICE_ID_LEN, out.deviceId.begin());

    // Nonce
    std::copy(data + 22, data + 22 + NONCE_LEN, out.nonce.begin());

    // Timestamp
    out.timestamp = readBigEndian64(data + 30);
    // Allow timestamp == 0 for devices without NTP sync yet
    // (they'll use uptime-based time which may start at 0 or small values)

    // CAN port
    out.canPort = readBigEndian16(data + 38);
    if (out.canPort == 0) {
        return false;
    }

    // OTA port
    out.otaPort = readBigEndian16(data + 40);

    // Signature
    std::copy(data + HEADER_LEN, data + HEADER_LEN + SIGNATURE_LEN,
              out.signature.begin());

    return true;
}

bool parse(const std::vector<uint8_t>& data, DiscoveryPacket& out) {
    return parse(data.data(), data.size(), out);
}

} // namespace vehicle_sim::discovery
