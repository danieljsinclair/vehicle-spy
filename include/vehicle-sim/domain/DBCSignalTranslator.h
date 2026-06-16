#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <mutex>
#include <unordered_map>
#include "vehicle-sim/domain/ISignalTranslator.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCParser.h"
#include "vehicle-sim/domain/VehicleSignalFactory.h"

namespace vehicle_sim::domain {

/**
 * DBC-driven signal translator.
 *
 * Bridges the ISignalTranslator interface (single-frame input) to
 * VehicleSignalFactory (map-of-all-frames input).
 *
 * This translator accumulates CAN frames across calls to translate(),
 * maintaining state for all supported CAN IDs until reset() is called.
 * Each translate() call returns a VehicleSignal representing the current
 * accumulated state.
 *
 * Frame format expected by translate():
 *   [canId_lo, canId_hi, data_byte_0, ..., data_byte_7]
 *   Minimum 10 bytes (2 ID + 8 data)
 *
 * Thread-safe via mutex on accumulatedFrames_.
 */
class DBCSignalTranslator final : public ISignalTranslator {
public:
    /**
     * Construct a DBC signal translator.
     *
     * @param config      Vehicle configuration with signal mappings
     * @param parseResult Parsed DBC file with signal definitions
     *
     * @pre config and parseResult must outlive the translator
     */
    DBCSignalTranslator(
        const VehicleConfig& config,
        const DBCParseResult& parseResult
    ) noexcept;

    ~DBCSignalTranslator() override = default;

    DBCSignalTranslator(const DBCSignalTranslator&) = delete;
    DBCSignalTranslator& operator=(const DBCSignalTranslator&) = delete;

    /**
     * Translate a raw CAN frame to VehicleSignal.
     *
     * Extracts CAN ID from the first 2 bytes (little-endian),
     * extracts the 8-byte data payload, stores it in accumulatedFrames_,
     * and delegates to VehicleSignalFactory.build().
     *
     * @param rawData       Raw CAN frame (minimum 10 bytes)
     * @param timestampUtcMs Optional original capture timestamp (epoch ms). When
     *                       supplied it is stamped onto the emitted signal;
     *                       otherwise wall-clock now() is used (live path).
     * @return VehicleSignal if frame is valid, nullopt otherwise
     */
    [[nodiscard]] std::optional<VehicleSignal> translate(
        const std::vector<std::uint8_t>& rawData,
        std::optional<std::uint64_t> timestampUtcMs = std::nullopt
    ) const noexcept override;

    /**
     * Validate whether a raw packet can be translated.
     *
     * @param rawData Raw CAN frame
     * @return true if frame size >= CAN_FRAME_SIZE (10 bytes), false otherwise
     */
    [[nodiscard]] bool isValidPacket(
        const std::vector<std::uint8_t>& rawData
    ) const noexcept override;

    /**
     * Get the list of supported CAN IDs from the DBC.
     *
     * @return Vector of CAN IDs that have signal definitions
     */
    [[nodiscard]] std::vector<std::uint16_t> getSupportedCANIds() const noexcept;

    /**
     * Reset accumulated state.
     *
     * Clears all stored frames, requiring re-accumulation.
     */
    void reset() noexcept;

private:
    VehicleSignalFactory factory_;
    const DBCParseResult& parseResult_;
    mutable std::mutex frames_mutex_;
    mutable std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> accumulatedFrames_;

    /**
     * Extract CAN ID from raw frame.
     *
     * Frame format: [canId_lo, canId_hi, data_byte_0, ...]
     * Returns 0 if frame is too short (< 2 bytes).
     *
     * @param frame Raw CAN frame
     * @return CAN ID (16-bit, little-endian)
     */
    [[nodiscard]] static std::uint16_t extractCANId(
        const std::vector<std::uint8_t>& frame
    ) noexcept;
};

} // namespace vehicle_sim::domain
