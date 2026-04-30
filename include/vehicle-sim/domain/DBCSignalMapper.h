#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include "vehicle-sim/domain/DBCSignalDefinition.h"

namespace vehicle_sim::domain {

/**
 * Generic CAN signal mapper.
 *
 * Maps a signal definition (from parsed DBC) + raw CAN bytes → physical value.
 *
 * This is the core decoder that applies scale, offset, byte order, and sign
 * conversion to extract meaningful physical values from raw CAN frames.
 *
 * Responsibilities:
 * - Extract raw bits according to start bit and bit length
 * - Apply byte order conversion (Motorola/Intel)
 * - Apply two's complement for signed signals
 * - Apply scale and offset to get physical value
 * - Clamp result to signal's min/max range
 *
 * This mapper is DBC-agnostic and vehicle-agnostic. It's a pure function
 * that takes a signal definition and data, returns a value.
 */
class DBCSignalMapper {
public:
    DBCSignalMapper() = delete;  // pure static utility

    /**
     * Map raw CAN bytes to a physical value using a signal definition.
     *
     * @param frame      Raw CAN data frame bytes (8 bytes for CAN 2.0)
     * @param definition Signal definition from parsed DBC
     * @return Physical value, or nullopt if frame too short or extraction fails
     */
    [[nodiscard]] static std::optional<double> mapSignal(
        const std::vector<std::uint8_t>& frame,
        const DBCSignalDefinition& definition
    ) noexcept;

    /**
     * Map raw CAN bytes to a physical value by CAN ID and signal name.
     *
     * Convenience overload when you have a map of definitions by ID.
     *
     * @param frame           Raw CAN data frame bytes
     * @param canId           CAN message ID
     * @param signalName      Name of signal to extract
     * @param definitions     Map of ID → signal definitions
     * @return Physical value, or nullopt if not found or extraction fails
     */
    [[nodiscard]] static std::optional<double> mapSignal(
        const std::vector<std::uint8_t>& frame,
        std::uint16_t canId,
        const std::string& signalName,
        const std::unordered_map<std::uint16_t,
            std::vector<DBCSignalDefinition>>& definitions
    ) noexcept;

private:
    /**
     * Extract raw bits from frame according to signal definition.
     *
     * Handles both Motorola (@1+) and Intel (@1-) byte orders.
     *
     * @param frame      Raw CAN data frame bytes
     * @param definition Signal definition containing bit position and order
     * @return Raw unsigned integer value
     */
    [[nodiscard]] static std::uint64_t extractRawBits(
        const std::vector<std::uint8_t>& frame,
        const DBCSignalDefinition& definition
    ) noexcept;

    /**
     * Convert raw unsigned to signed if signal is signed.
     *
     * Applies two's complement sign extension.
     *
     * @param raw       Raw unsigned value
     * @param bitLength Width of the signal in bits
     * @return Signed value (two's complement)
     */
    [[nodiscard]] static std::int64_t toSigned(
        std::uint64_t raw,
        std::size_t bitLength
    ) noexcept;
};

} // namespace vehicle_sim::domain
