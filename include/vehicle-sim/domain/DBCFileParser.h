#pragma once

#include "vehicle-sim/domain/DBCParser.h"

namespace vehicle_sim::domain {

/**
 * Concrete DBC file parser.
 *
 * Parses standard .dbc text files into DBCParseResult structures.
 * Supports BO_ (message), SG_ (signal), and VAL_ (value table) lines.
 */
class DBCFileParser final : public DBCParser {
public:
    [[nodiscard]] DBCParseResult parseFile(
        const std::string& dbcFilePath
    ) const noexcept override;

    [[nodiscard]] DBCParseResult parseString(
        const std::string& dbcContent
    ) const noexcept override;

    [[nodiscard]] bool canParse(
        const std::string& dbcFilePath
    ) const noexcept override;
};

} // namespace vehicle_sim::domain
