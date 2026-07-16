#include "vehicle-sim/domain/DBCSignalMapper.h"
#include "vehicle-sim/domain/CANDecoder.h"
#include "vehicle-sim/domain/Gear.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace vehicle_sim::domain {

namespace {

/**
 * Apply scale, offset, and clamp to a raw signal value.
 *
 * Converts a raw unsigned or signed value to a physical value by applying
 * scale and offset, then clamps to the signal's defined min/max range.
 *
 * @param rawBits    Raw unsigned value (before sign conversion)
 * @param definition Signal definition containing scale, offset, min, max, signedness
 * @return Physical value clamped to [min, max], or nullopt if extraction fails
 */
[[nodiscard]] std::optional<double> applyScaleOffsetClamp(
    std::uint64_t rawBits,
    const DBCSignalDefinition& definition
) noexcept {
    double physical;

    if (definition.isSigned) {
        const auto signedVal = CANDecoder::toSigned(rawBits, definition.bitLength);
        physical = static_cast<double>(signedVal) * definition.scale + definition.offset;
    } else {
        physical = static_cast<double>(rawBits) * definition.scale + definition.offset;
    }

    return std::clamp(physical, definition.min, definition.max);
}

} // namespace

namespace {

/**
 * Map a raw value to a Gear constant using a value table.
 *
 * Searches the value table for a matching raw value and maps its description
 * to the canonical Gear constant. Returns nullopt if no match is found.
 *
 * @param rawValue   Raw value from CAN signal
 * @param valueTable Value table entries from DBC VAL_ definitions
 * @return Gear constant, or nullopt if no matching description
 */
[[nodiscard]] std::optional<std::int32_t> mapValueTableToGear(
    std::int64_t rawValue,
    const std::vector<DBCValueEntry>& valueTable
) noexcept {
    for (const auto& entry : valueTable) {
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
    return std::nullopt;
}

} // namespace

// Serialization-edge adapter: view a uint8_t frame as byte-oriented data for the
// bit-extraction helpers, without changing the wire format.
namespace {
[[nodiscard]] std::vector<std::byte> toByteView(const std::vector<std::uint8_t>& frame) {
    std::vector<std::byte> bytes(frame.size());
    std::transform(frame.begin(), frame.end(), bytes.begin(),
                   [](std::uint8_t b) { return static_cast<std::byte>(b); });
    return bytes;
}
} // namespace

std::optional<double> DBCSignalMapper::mapSignal(
    const std::vector<std::uint8_t>& frame,
    const DBCSignalDefinition& definition
) noexcept {
    if (const std::size_t lastBit = definition.startBit + definition.bitLength - 1; lastBit >= frame.size() * 8) {
        return std::nullopt;
    }

    const auto rawBits = extractRawBits(toByteView(frame), definition);
    return applyScaleOffsetClamp(rawBits, definition);
}

std::optional<double> DBCSignalMapper::mapSignal(
    const std::vector<std::uint8_t>& frame,
    std::uint16_t canId,
    std::string_view signalName,
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
    std::string_view signalName,
    const std::unordered_map<std::uint16_t,
        std::vector<DBCSignalDefinition>>& definitions
) noexcept {
    auto it = definitions.find(canId);
    if (it == definitions.end()) {
        return std::nullopt;
    }

    for (const auto& def : it->second) {
        if (def.name == signalName) {
            const std::uint64_t rawBits = extractRawBits(toByteView(frame), def);
            const auto rawValue = static_cast<std::int64_t>(rawBits);

            // Check for INVALID (0) or SNA (7) - return nullopt
            if (rawValue == 0 || rawValue == 7) {
                return std::nullopt;
            }

            // Check if signal has a value table
            if (def.valueTable.empty()) {
                // No value table - use direct mapping for simple cases
                // (This shouldn't happen for DI_gear in Tesla Model 3)
                return static_cast<std::int32_t>(rawBits);
            }

            // Use value table for translation
            return mapValueTableToGear(rawValue, def.valueTable);
        }
    }
    return std::nullopt;
}

std::uint64_t DBCSignalMapper::extractRawBits(
    const std::vector<std::byte>& frame,
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

            if (byteIdx < frame.size() &&
                ((frame[byteIdx] & (std::byte{1} << bitIdx)) != std::byte{0})) {
                result |= (1ULL << i);
            }
        }
    } else {
        // Motorola (@0): DBC sawtooth bit numbering.
        //
        // In the DBC convention (Vector CANdb++, commaai/opendbc), the
        // startBit of a big-endian signal is the position of its MOST
        // significant bit. The DBC frame is numbered sawtooth: within each
        // byte, DBC bit (8*k + 7) is the physical MSB and DBC bit (8*k) is
        // the physical LSB — i.e. DBC bit n in byte n/8 sits at physical
        // bit (n % 8). The signal bits progress from the MSB (at startBit)
        // DOWNWARD in physical significance: across a byte they decrement
        // the DBC position, and on reaching the LSB of a byte (n % 8 == 0)
        // they wrap to the MSB of the next byte (n += 15).
        //
        // Verified against cantools 41.4.3 across every Motorola signal in
        // Model3CAN.dbc (7600 random-frame cases, zero mismatches).
        std::size_t dbcBit = definition.startBit;
        for (std::size_t i = 0; i < definition.bitLength; ++i) {
            const std::size_t byteIdx = dbcBit / 8;
            const std::size_t bitInByte = dbcBit % 8;
            const std::size_t resultBit = definition.bitLength - 1 - i;

            if (byteIdx < frame.size() &&
                ((frame[byteIdx] & (std::byte{1} << bitInByte)) != std::byte{0})) {
                result |= (1ULL << resultBit);
            }

            // Advance to the next signal bit (toward the LSB). Wrapping from
            // the physical LSB of a byte to the physical MSB of the next byte
            // is a +15 jump in DBC numbering; otherwise step down by 1.
            dbcBit = (dbcBit % 8 == 0) ? dbcBit + 15 : dbcBit - 1;
        }
    }

    return result;
}

} // namespace vehicle_sim::domain
