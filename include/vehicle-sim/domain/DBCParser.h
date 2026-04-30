#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include "vehicle-sim/domain/DBCSignalDefinition.h"

namespace vehicle_sim::domain {

/**
 * Parsing result for a complete DBC file.
 *
 * Organizes signal definitions by CAN ID for efficient lookup.
 * Signals are stored in a map keyed by CAN ID for O(1) access.
 */
struct DBCParseResult final {
    /**
     * Map of CAN ID to vector of signal definitions for that message.
     *
     * Example: { 264 => [DIR_axleSpeed, DIR_torqueActual, ...] }
     */
    std::unordered_map<std::uint16_t, std::vector<DBCSignalDefinition>> signalsByCanId;

    /**
     * Get all signal definitions for a specific CAN ID.
     *
     * @param canId The CAN message ID
     * @return Pointer to vector of signals, or nullptr if not found
     */
    [[nodiscard]] const std::vector<DBCSignalDefinition>* getSignalsForCanId(
        std::uint16_t canId
    ) const noexcept;

    /**
     * Get total number of parsed signals.
     */
    [[nodiscard]] std::size_t totalSignalCount() const noexcept;
};

/**
 * DBC file parser interface.
 *
 * Parses .dbc (Database Container) files into structured signal definitions.
 * Returns data only, no imperative decode logic.
 *
 * Supports DBC format elements:
 * - BO_ : Message definition (ID, name, length)
 * - SG_ : Signal definition within a message
 * - VAL_: Value table / enum mapping
 *
 * This is an interface to allow multiple parser implementations:
 * - Text file parser (primary)
 * - Binary cache parser (performance optimization)
 * - Test mock parser (for unit testing)
 */
class DBCParser {
public:
    virtual ~DBCParser() = default;

    /**
     * Parse a DBC file and extract all signal definitions.
     *
     * @param dbcFilePath Path to the .dbc file
     * @return Parse result with signals organized by CAN ID, or error state
     *
     * @note Parse failures should be indicated by empty result.
     *       Consider adding error reporting interface in future.
     */
    [[nodiscard]] virtual DBCParseResult parseFile(
        const std::string& dbcFilePath
    ) const noexcept = 0;

    /**
     * Parse DBC content from a string buffer.
     *
     * Useful for testing or in-memory DBC sources.
     *
     * @param dbcContent The full DBC file content as a string
     * @return Parse result with signals organized by CAN ID
     */
    [[nodiscard]] virtual DBCParseResult parseString(
        const std::string& dbcContent
    ) const noexcept = 0;

    /**
     * Validate if a DBC file can be parsed.
     *
     * Lightweight check without full parsing.
     *
     * @param dbcFilePath Path to the .dbc file
     * @return true if file exists and appears to be valid DBC format
     */
    [[nodiscard]] virtual bool canParse(
        const std::string& dbcFilePath
    ) const noexcept = 0;
};

} // namespace vehicle_sim::domain
