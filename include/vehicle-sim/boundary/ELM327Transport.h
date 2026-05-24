#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace vehicle_sim::boundary {

/**
 * @brief ELM327 AT command with delay specification.
 */
struct ATCommand {
    std::string command;  // Full AT command string with CR (e.g., "ATZ\r")
    int delayMs;         // Delay after command in milliseconds
};

/**
 * @brief CAN frame parsed from ELM327 monitor mode.
 *
 * Represents a raw CAN bus frame with ID and data payload.
 */
struct CANFrame {
    uint16_t canId;
    std::vector<uint8_t> data; // 8 bytes for standard CAN frames
};

/**
 * @brief ELM327 OBD2 transport layer.
 *
 * Handles ASCII encoding/decoding for ELM327-compatible adapters.
 * ELM327 uses ASCII hex commands and responses, not binary.
 *
 * Example conversions:
 *   Query:  PID 0x01 0x0C → "01 0C\r"
 *   Response: "41 0C 1A F8\r" → binary [0x41, 0x0C, 0x1A, 0xF8]
 */
class ELM327Transport {
public:
    /**
     * @brief Build an OBD2 PID query as ASCII hex string.
     * @param mode OBD2 mode (e.g., 0x01 for live data, 0x09 for vehicle info)
     * @param pid Parameter ID to query
     * @return ASCII command string with space-separated hex and CR terminator
     */
    static std::string buildOBD2Query(uint8_t mode, uint8_t pid);

    /**
     * @brief Parse an ELM327 ASCII hex response to binary.
     * @param response ASCII hex string from ELM327 (may include prefixes, prompts, etc.)
     * @return Binary response bytes, or nullopt if response is invalid/error
     */
    static std::optional<std::vector<uint8_t>> parseOBD2Response(const std::string& response);

    /**
     * @brief Build an AT command string.
     * @param command AT command without "AT" prefix or terminator (e.g., "Z", "E0")
     * @return Full AT command with "AT" prefix and CR terminator
     */
    static std::string buildATCommand(const std::string& command);

    /**
     * @brief Parse AT command response.
     * @param response Response from AT command
     * @return true if command succeeded, false if error
     */
    static bool parseATResponse(const std::string& response);

    /**
     * @brief Build ELM327 initialization command sequence.
     * @return Sequence of AT commands with timing requirements
     */
    static std::vector<ATCommand> buildInitSequence();

    /** Init sequence for VIN query — uses ATSP6 (specific protocol) instead of ATSP0. */
    static std::vector<ATCommand> buildVINQueryInitSequence();

    /**
     * @brief Remove ELM327 prompt character if present.
     * @param response Response possibly containing trailing '>' prompt
     * @return Response without trailing prompt
     */
    static std::string extractPrompt(const std::string& response);

    /**
     * @brief Build CAN monitor mode initialization sequence.
     * Configures ELM327 for raw CAN monitoring (Tesla uses this).
     * @return Sequence of AT commands with timing requirements:
     *         ATZ, ATE0, ATSP6 (ISO 15765-4 CAN 500kbps), ATH1 (headers on), ATMA (monitor all)
     */
    static std::vector<ATCommand> buildCANMonitorInitSequence();

    /**
     * @brief Build CAN filter command for specific CAN ID.
     * @param canId The CAN ID to filter for (11-bit)
     * @return ATCRA command string (e.g., "ATCRA264\r")
     */
    static std::string buildCANFilter(uint16_t canId);

    /**
     * @brief Parse CAN frame from ELM327 monitor mode output.
     * ELM327 in monitor mode with headers outputs lines like:
     *   "610 264 00 00 00 90 01 10 27 00" (with type prefix)
     *   "264 00 00 00 90 01 10 27 00" (without type prefix)
     * Where the first hex is the CAN ID followed by 8 data bytes.
     * @param line ASCII line from ELM327
     * @return Parsed CAN frame, or nullopt if not a valid CAN frame
     */
    static std::optional<CANFrame> parseCANFrame(const std::string& line);
};

} // namespace vehicle_sim::boundary
