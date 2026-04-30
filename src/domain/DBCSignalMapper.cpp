#include "vehicle-sim/domain/DBCSignalMapper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vehicle_sim::domain {

std::optional<double> DBCSignalMapper::mapSignal(
    const std::vector<std::uint8_t>& frame,
    const DBCSignalDefinition& definition
) noexcept {
    const std::size_t lastBit = definition.startBit + definition.bitLength - 1;
    if (lastBit >= frame.size() * 8) {
        return std::nullopt;
    }

    const auto rawBits = extractRawBits(frame, definition);
    if (definition.isSigned) {
        const auto signedVal = toSigned(rawBits, definition.bitLength);
        double physical = static_cast<double>(signedVal) * definition.scale + definition.offset;
        return std::clamp(physical, definition.min, definition.max);
    }
    double physical = static_cast<double>(rawBits) * definition.scale + definition.offset;
    return std::clamp(physical, definition.min, definition.max);
}

std::optional<double> DBCSignalMapper::mapSignal(
    const std::vector<std::uint8_t>& frame,
    std::uint16_t canId,
    const std::string& signalName,
    const std::unordered_map<std::uint16_t,
        std::vector<DBCSignalDefinition>>& definitions
) noexcept {
    auto it = definitions.find(canId);
    if (it == definitions.end()) return std::nullopt;

    for (const auto& def : it->second) {
        if (def.name == signalName) {
            return mapSignal(frame, def);
        }
    }
    return std::nullopt;
}

std::uint64_t DBCSignalMapper::extractRawBits(
    const std::vector<std::uint8_t>& frame,
    const DBCSignalDefinition& definition
) noexcept {
    if (definition.bitLength == 0 || definition.bitLength > 64) return 0;

    std::uint64_t result = 0;

    if (definition.byteOrder == DBCByteOrder::Intel) {
        // Intel (@1): bit 0 = LSB of byte 0, sequential across bytes
        for (std::size_t i = 0; i < definition.bitLength; ++i) {
            const std::size_t bitPos = definition.startBit + i;
            const std::size_t byteIdx = bitPos / 8;
            const std::size_t bitIdx = bitPos % 8;

            if (byteIdx < frame.size() && (frame[byteIdx] & (1ULL << bitIdx))) {
                result |= (1ULL << i);
            }
        }
    } else {
        // Motorola (@0): bit numbering starts from MSB
        const std::size_t startByte = definition.startBit / 8;
        const std::size_t startBitInByte = definition.startBit % 8;

        std::uint64_t raw = 0;
        for (std::size_t b = 0; b < frame.size() && b < 8; ++b) {
            raw |= static_cast<std::uint64_t>(frame[b]) << (b * 8);
        }

        // Extract bits in Motorola order (MSB first within signal)
        std::uint64_t mask = (definition.bitLength == 64)
            ? ~0ULL
            : (1ULL << definition.bitLength) - 1;

        std::size_t msbBitPos = definition.startBit;
        std::size_t lsbBitPos = definition.startBit + definition.bitLength - 1;
        std::size_t shift = lsbBitPos;

        result = (raw >> shift) & mask;
    }

    return result;
}

std::int64_t DBCSignalMapper::toSigned(
    std::uint64_t raw,
    std::size_t bitLength
) noexcept {
    if (bitLength >= 64) return static_cast<std::int64_t>(raw);

    const std::uint64_t signBit = 1ULL << (bitLength - 1);
    if (raw & signBit) {
        raw |= ~((1ULL << bitLength) - 1);
    }
    return static_cast<std::int64_t>(raw);
}

} // namespace vehicle_sim::domain
