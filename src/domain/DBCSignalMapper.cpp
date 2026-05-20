#include "vehicle-sim/domain/DBCSignalMapper.h"
#include "vehicle-sim/domain/CANDecoder.h"
#include "vehicle-sim/domain/Gear.h"

#include <algorithm>
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
        const auto signedVal = CANDecoder::toSigned(rawBits, definition.bitLength);
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

std::optional<std::int32_t> DBCSignalMapper::mapGearSignal(
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
            // Extract raw value
            std::uint64_t rawBits = extractRawBits(frame, def);

            // Check if signal has a value table
            if (def.valueTable.empty()) {
                // No value table - use direct mapping for simple cases
                // (This shouldn't happen for DI_gear in Tesla Model 3)
                if (rawBits == 0 || rawBits == 7) {
                    return std::nullopt;
                }
                return static_cast<std::int32_t>(rawBits);
            }

            // Use value table for translation
            // Tesla Model 3/Y DI_gear VAL_ table:
            // 0 "DI_GEAR_INVALID" → nullopt
            // 1 "DI_GEAR_P" → Gear::PARK
            // 2 "DI_GEAR_R" → Gear::REVERSE
            // 3 "DI_GEAR_N" → Gear::NEUTRAL
            // 4 "DI_GEAR_D" → Gear::AUTO_1
            // 7 "DI_GEAR_SNA" → nullopt
            std::int64_t rawValue = static_cast<std::int64_t>(rawBits);

            // Check for INVALID (0) or SNA (7) - return nullopt
            if (rawValue == 0 || rawValue == 7) {
                return std::nullopt;
            }

            // Map to Gear constants based on value description
            for (const auto& entry : def.valueTable) {
                if (entry.value == rawValue) {
                    if (entry.description == "DI_GEAR_P") {
                        return Gear::PARK;
                    } else if (entry.description == "DI_GEAR_R") {
                        return Gear::REVERSE;
                    } else if (entry.description == "DI_GEAR_N") {
                        return Gear::NEUTRAL;
                    } else if (entry.description == "DI_GEAR_D") {
                        return Gear::AUTO_1;
                    }
                }
            }

            // Unknown value - return nullopt
            return std::nullopt;
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
        // Motorola (@0): MSB at startBit, reversed bit numbering within bytes
        // DBC bit n → byte = n/8, bit_within_byte = 7 - (n%8)
        // Signal MSB at startBit, bits progress in increasing DBC position
        for (std::size_t i = 0; i < definition.bitLength; ++i) {
            const std::size_t dbcBit = definition.startBit + i;
            const std::size_t byteIdx = dbcBit / 8;
            const std::size_t bitInByte = 7 - (dbcBit % 8);
            const std::size_t resultBit = definition.bitLength - 1 - i;

            if (byteIdx < frame.size() && (frame[byteIdx] & (1ULL << bitInByte))) {
                result |= (1ULL << resultBit);
            }
        }
    }

    return result;
}

} // namespace vehicle_sim::domain
