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

    /**
     * @brief Remove ELM327 prompt character if present.
     * @param response Response possibly containing trailing '>' prompt
     * @return Response without trailing prompt
     */
    static std::string extractPrompt(const std::string& response);
};

} // namespace vehicle_sim::boundary
