#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <mutex>

#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Parser for Tesla CAN bus signals
 *
 * Parses raw CAN frames from Tesla vehicles and extracts vehicle telemetry.
 * Supports Model 3/Y signal definitions via DBC file format.
 *
 * Thread-safe: All methods are safe for concurrent calls from multiple threads.
 *
 * CAN Frame Structure:
 * - ID: 11-bit (standard) or 29-bit (extended)
 * - DLC: Data Length Code (0-8 bytes)
 * - Data: Signal payload
 * - Checksum: Frame validation
 */
class TeslaSignalParser final {
public:
    /**
     * Callback type for parsed vehicle signals
     */
    using SignalCallback = std::function<void(const VehicleSignal&)>;

    /**
     * Construct a parser with optional callback
     * @param callback Function to call when a signal is parsed (optional)
     */
    explicit TeslaSignalParser(SignalCallback callback = nullptr);

    /**
     * Parse a complete CAN frame
     * @param frame Raw CAN frame bytes
     * @return Parsed signal if valid, empty otherwise
     */
    [[nodiscard]] std::optional<VehicleSignal> parseFrame(
        const std::vector<std::uint8_t>& frame
    ) const;

    /**
     * Feed raw bytes to the parser (handles partial frames)
     * @param data Raw bytes from transport
     * @return Vector of complete frames parsed (may be empty)
     */
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> feedData(
        const std::vector<std::uint8_t>& data
    );

    /**
     * Validate CAN frame structure
     * @param frame Frame to validate
     * @return true if frame is valid
     */
    [[nodiscard]] bool isValidFrame(const std::vector<std::uint8_t>& frame) const;

    /**
     * Extract CAN ID from frame
     * @param frame CAN frame
     * @return CAN ID (11 or 29 bits)
     */
    [[nodiscard]] std::uint32_t extractCANId(
        const std::vector<std::uint8_t>& frame
    ) const;

    /**
     * Check if frame has extended CAN ID
     * @param frame CAN frame
     * @return true if extended (29-bit) ID
     */
    [[nodiscard]] bool isExtendedCANId(
        const std::vector<std::uint8_t>& frame
    ) const;

    /**
     * Calculate frame checksum
     * @param frame CAN frame
     * @return Calculated checksum
     */
    [[nodiscard]] std::uint8_t calculateChecksum(
        const std::vector<std::uint8_t>& frame
    ) const;

    /**
     * Set signal callback
     * @param callback Function to call when signal is parsed
     */
    void setSignalCallback(SignalCallback callback);

    /**
     * Clear internal buffer (for error recovery)
     */
    void clearBuffer();

private:
    SignalCallback m_callback;
    std::vector<std::uint8_t> m_buffer;
    mutable std::mutex m_mutex;

    /**
     * Extract vehicle speed from CAN data
     * @param data CAN data bytes
     * @return Speed in km/h
     */
    [[nodiscard]] double extractSpeed(
        const std::vector<std::uint8_t>& data
    ) const;

    /**
     * Extract throttle percentage from CAN data
     * @param data CAN data bytes
     * @return Throttle percentage (0-100)
     */
    [[nodiscard]] double extractThrottle(
        const std::vector<std::uint8_t>& data
    ) const;

    /**
     * Extract brake percentage from CAN data
     * @param data CAN data bytes
     * @return Brake percentage (0-100)
     */
    [[nodiscard]] double extractBrake(
        const std::vector<std::uint8_t>& data
    ) const;

    /**
     * Extract acceleration from CAN data
     * @param data CAN data bytes
     * @return Acceleration in G
     */
    [[nodiscard]] double extractAcceleration(
        const std::vector<std::uint8_t>& data
    ) const;

    /**
     * Get current timestamp
     * @return Unix timestamp in milliseconds
     */
    [[nodiscard]] std::uint64_t getCurrentTimestamp() const;
};

} // namespace vehicle_sim::domain
