#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vehicle_sim::domain {

/**
 * DBC byte order specification.
 *
 * In DBC format: @0 = Motorola (MSB first), @1 = Intel (LSB first)
 * Sign suffix (+/-) is a separate concern (isSigned field).
 * All Tesla Model 3/Y signals use @1 (Intel).
 */
enum class DBCByteOrder : uint8_t {
    Motorola = 0,  // @0 in DBC - big-endian (MSB first)
    Intel = 1      // @1 in DBC - little-endian (LSB first)
};

/**
 * Value table entry for enum-like signals.
 *
 * Maps a numeric raw value to a descriptive string.
 * Used for VAL_ entries in DBC files.
 */
struct DBCValueEntry {
    std::int64_t value;
    std::string   description;
};

/**
 * Parameter object for DBC signal definition construction.
 *
 * Groups all signal parameters into a single struct to improve
 * readability and maintainability of construction calls.
 */
struct DBCSignalParams {
    std::uint16_t canId = 0;
    std::string name;
    std::size_t startBit = 0;
    std::size_t bitLength = 0;
    DBCByteOrder byteOrder = DBCByteOrder::Intel;
    double scale = 0.0;
    double offset = 0.0;
    bool isSigned = false;
    std::string unit;
    double min = 0.0;
    double max = 0.0;
    std::vector<DBCValueEntry> valueTable{};
};

/**
 * Signal definition extracted from a DBC file.
 *
 * Represents one signal within a CAN message, containing all
 * parameters needed to decode raw CAN bytes to physical values.
 *
 * Immutable value object - all fields const.
 */
struct DBCSignalDefinition final {
    /**
     * Construct a complete signal definition from params.
     *
     * @param params Struct containing all signal parameters
     */
    explicit DBCSignalDefinition(
        const DBCSignalParams& params
    ) noexcept;

    // Message identifier (BO_ from DBC)
    const std::uint16_t canId;

    // Signal name (SG_ from DBC)
    const std::string name;

    // Bit position within CAN frame (Intel convention: bit 0 = LSB of byte 0)
    const std::size_t startBit;

    // Width of signal in bits (1-64)
    const std::size_t bitLength;

    // Byte order: Motorola (@1+) or Intel (@1-)
    const DBCByteOrder byteOrder;

    // Scaling factor: physical = raw * scale + offset
    const double scale;

    // Offset applied after scaling
    const double offset;

    // True for signed two's complement interpretation
    const bool isSigned;

    // Physical unit string (e.g., "RPM", "Nm", "kph")
    const std::string unit;

    // Minimum valid physical value
    const double min;

    // Maximum valid physical value
    const double max;

    // Optional value table for enum-like signals (VAL_ entries)
    const std::vector<DBCValueEntry> valueTable;

    // Copy/move construction OK, assignment deleted due to const members
    DBCSignalDefinition(const DBCSignalDefinition&) noexcept = default;
    DBCSignalDefinition(DBCSignalDefinition&&) noexcept = default;
    DBCSignalDefinition& operator=(const DBCSignalDefinition&) = delete;
    DBCSignalDefinition& operator=(DBCSignalDefinition&&) = delete;
    ~DBCSignalDefinition() noexcept = default;

    // Equality comparison (all fields)
    [[nodiscard]] friend bool operator==(
        const DBCSignalDefinition& lhs,
        const DBCSignalDefinition& rhs
    ) noexcept {
        if (lhs.canId != rhs.canId
            || lhs.name != rhs.name
            || lhs.startBit != rhs.startBit
            || lhs.bitLength != rhs.bitLength
            || lhs.byteOrder != rhs.byteOrder
            || lhs.scale != rhs.scale
            || lhs.offset != rhs.offset
            || lhs.isSigned != rhs.isSigned
            || lhs.unit != rhs.unit
            || lhs.min != rhs.min
            || lhs.max != rhs.max) {
            return false;
        }

        if (lhs.valueTable.size() != rhs.valueTable.size()) {
            return false;
        }

        for (std::size_t i = 0; i < lhs.valueTable.size(); ++i) {
            if (lhs.valueTable[i].value != rhs.valueTable[i].value
                || lhs.valueTable[i].description != rhs.valueTable[i].description) {
                return false;
            }
        }

        return true;
    }

    // Inequality (derived from operator==)
    [[nodiscard]] friend bool operator!=(
        const DBCSignalDefinition& lhs,
        const DBCSignalDefinition& rhs
    ) noexcept {
        return !(lhs == rhs);
    }
};

} // namespace vehicle_sim::domain
