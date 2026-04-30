#include "vehicle-sim/domain/CANDecoder.h"

namespace vehicle_sim::domain {

std::optional<double> CANDecoder::extractSignal(
    const std::vector<uint8_t>& frame,
    std::size_t startBit,
    std::size_t bitLength,
    double scale,
    double offset,
    bool isSigned
) noexcept {
    if (bitLength == 0 || bitLength > 64) {
        return std::nullopt;
    }

    const std::size_t lastBit = startBit + bitLength - 1;
    if (lastBit >= frame.size() * 8) {
        return std::nullopt;
    }

    std::uint64_t raw = extractRawBits(frame, startBit, bitLength);

    double physical;
    if (isSigned) {
        physical = static_cast<double>(toSigned(raw, bitLength));
    } else {
        physical = static_cast<double>(raw);
    }

    return physical * scale + offset;
}

std::uint64_t CANDecoder::extractRawBits(
    const std::vector<uint8_t>& frame,
    std::size_t startBit,
    std::size_t bitLength
) noexcept {
    std::uint64_t result = 0;

    for (std::size_t i = 0; i < bitLength; ++i) {
        const std::size_t bitPos = startBit + i;
        const std::size_t byteIdx = bitPos / 8;
        const std::size_t bitIdx = bitPos % 8;

        if (frame[byteIdx] & (1u << bitIdx)) {
            result |= (1ULL << i);
        }
    }

    return result;
}

std::int64_t CANDecoder::toSigned(
    std::uint64_t raw,
    std::size_t bitLength
) noexcept {
    if (bitLength >= 64) {
        return static_cast<std::int64_t>(raw);
    }

    const std::uint64_t signBit = 1ULL << (bitLength - 1);
    if (raw & signBit) {
        // Two's complement: extend sign
        raw |= ~((1ULL << bitLength) - 1);
        return static_cast<std::int64_t>(raw);
    }

    return static_cast<std::int64_t>(raw);
}

} // namespace vehicle_sim::domain
