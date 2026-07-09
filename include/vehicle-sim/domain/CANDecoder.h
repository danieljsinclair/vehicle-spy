#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <cstddef>

namespace vehicle_sim::domain {

/**
 * DBC-aware CAN frame signal extractor.
 *
 * Extracts a signal value from raw CAN frame bytes using parameters
 * defined in DBC (Database Container) files:
 *   - startBit:  bit position within the frame (Intel/motorola byte order)
 *   - bitLength: width of the signal in bits
 *   - scale:     physical = raw * scale + offset
 *   - offset:    applied after scaling
 *   - isSigned:  true for signed two's complement signals
 *
 * Bit numbering follows the Intel/little-endian convention used in
 * DBC files exported by Vector CANdb++, commaai/opendbc, etc.
 * Bit 0 is the LSB of byte 0.
 *
 * All CAN frames are assumed to be 8 bytes (CAN 2.0 standard).
 */
class CANDecoder {
public:
    CANDecoder() = delete;  // pure static utility

    /**
     * Extract a signal from a CAN data frame.
     *
     * @param frame     8-byte CAN data payload (or longer for CAN FD)
     * @param startBit  bit position of signal LSB (Intel convention)
     * @param bitLength width of signal in bits (1-64)
     * @param scale     scaling factor
     * @param offset    offset applied after scaling
     * @param isSigned  true for two's complement interpretation
     * @return physical value, or nullopt if frame is too short
     */
    [[nodiscard]] static std::optional<double> extractSignal(
        const std::vector<std::byte>& frame,
        std::size_t startBit,
        std::size_t bitLength,
        double scale,
        double offset,
        bool isSigned
    ) noexcept;

    /**
     * Convert raw unsigned to signed using two's complement sign extension.
     *
     * @param raw       Raw unsigned value
     * @param bitLength Width of the signal in bits
     * @return Signed value (two's complement)
     */
    [[nodiscard]] static std::int64_t toSigned(
        std::uint64_t raw,
        std::size_t bitLength
    ) noexcept;

private:
    [[nodiscard]] static std::uint64_t extractRawBits(
        const std::vector<std::byte>& frame,
        std::size_t startBit,
        std::size_t bitLength
    ) noexcept;
};

} // namespace vehicle_sim::domain
