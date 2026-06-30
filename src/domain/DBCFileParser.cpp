#include "vehicle-sim/domain/DBCFileParser.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace vehicle_sim::domain {

namespace {

std::string trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

// Advance `pos` past any run of spaces/tabs in `s`. A no-op cursor primitive
// shared by the line parsers to keep the cursor walk uniform.
void skipWs(std::string_view s, std::size_t& pos) noexcept {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
}

// If the cursor is on `expected`, advance past it and return true; otherwise
// leave the cursor in place and return false (delimiter-check primitive).
bool consumeChar(std::string_view s, std::size_t& pos, char expected) noexcept {
    if (pos < s.size() && s[pos] == expected) {
        ++pos;
        return true;
    }
    return false;
}

struct ParsedSignal {
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
    std::vector<DBCValueEntry> valueTable;
};

// Read the signal name and an optional multiplexor indicator ('M' or 'm'+digits)
// that may follow it. Advances `pos` past both, leaving it on the ':' delimiter.
[[nodiscard]] bool parseNameAndMultiplexor(
    const std::string& line, std::size_t& pos, ParsedSignal& out
) {
    std::size_t nameEnd = pos;
    while (nameEnd < line.size() && line[nameEnd] != ' ' && line[nameEnd] != ':') ++nameEnd;
    out.name = line.substr(pos, nameEnd - pos);
    pos = nameEnd;

    skipWs(line, pos);
    if (pos < line.size() && (line[pos] == 'M' || line[pos] == 'm')) {
        if (line[pos] == 'm') {
            ++pos;
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
        } else {
            ++pos;
        }
        skipWs(line, pos);
    }
    return true;
}

// Parse "<startBit>|<bitLength>@<order><sign>" — the bit-layout group. The
// order char is '0' for Motorola, else Intel; the sign char is '-' for signed.
[[nodiscard]] bool parseStartBitAndLength(
    const std::string& line, std::size_t& pos, ParsedSignal& out
) {
    if (!consumeChar(line, pos, ':')) return false;
    skipWs(line, pos);

    auto pipePos = line.find('|', pos);
    if (pipePos == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, pipePos - pos);
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out.startBit);
        if (ec != std::errc{}) return false;
    }

    pos = pipePos + 1;
    auto atPos = line.find('@', pos);
    if (atPos == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, atPos - pos);
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out.bitLength);
        if (ec != std::errc{}) return false;
    }

    pos = atPos + 1;
    if (pos >= line.size()) return false;
    out.byteOrder = (line[pos] == '0') ? DBCByteOrder::Motorola : DBCByteOrder::Intel;
    ++pos;

    if (pos >= line.size()) return false;
    out.isSigned = (line[pos] == '-');
    ++pos;
    return true;
}

// Parse the "(<scale>,<offset>)" group. Values are trimmed before stod.
[[nodiscard]] bool parseScaleOffset(
    const std::string& line, std::size_t& pos, ParsedSignal& out
) {
    skipWs(line, pos);
    if (!consumeChar(line, pos, '(')) return false;

    auto commaPos = line.find(',', pos);
    if (commaPos == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, commaPos - pos);
        out.scale = std::stod(trim(sv));
    }
    pos = commaPos + 1;

    auto closeParen = line.find(')', pos);
    if (closeParen == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, closeParen - pos);
        out.offset = std::stod(trim(sv));
    }
    pos = closeParen + 1;
    return true;
}

// Parse the "[<min>|<max>]" group. Values are trimmed before stod.
[[nodiscard]] bool parseMinMax(
    const std::string& line, std::size_t& pos, ParsedSignal& out
) {
    skipWs(line, pos);
    if (!consumeChar(line, pos, '[')) return false;

    auto pipePos = line.find('|', pos);
    if (pipePos == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, pipePos - pos);
        out.min = std::stod(trim(sv));
    }
    pos = pipePos + 1;

    auto closeBracket = line.find(']', pos);
    if (closeBracket == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, closeBracket - pos);
        out.max = std::stod(trim(sv));
    }
    pos = closeBracket + 1;
    return true;
}

// Parse the optional `"unit"` field. A missing closing quote leaves the unit
// empty (matches the previous behaviour).
[[nodiscard]] bool parseUnit(
    const std::string& line, std::size_t& pos, ParsedSignal& out
) {
    skipWs(line, pos);
    if (!consumeChar(line, pos, '"')) return true;  // unit is optional

    auto endQuote = line.find('"', pos);
    if (endQuote != std::string::npos) {
        out.unit = line.substr(pos, endQuote - pos);
        pos = endQuote + 1;
    }
    return true;
}

bool parseSignalDefinition(
    const std::string& line,
    std::uint16_t currentCanId,
    ParsedSignal& out
) {
    auto pos = line.find("SG_");
    if (pos == std::string::npos) return false;
    pos += 3;
    skipWs(line, pos);

    if (!parseNameAndMultiplexor(line, pos, out)) return false;
    if (!parseStartBitAndLength(line, pos, out)) return false;
    if (!parseScaleOffset(line, pos, out)) return false;
    if (!parseMinMax(line, pos, out)) return false;
    if (!parseUnit(line, pos, out)) return false;

    out.canId = currentCanId;
    return true;
}

// Parse one "<num> \"<label>\"" value-table entry from `pos` in `rest`. Each
// malformed-input or end-of-input condition returns false; on success the entry
// is appended to `entries` and `pos` is advanced, returning true to continue.
bool parseOneValueEntry(const std::string& rest, std::size_t& pos,
                        std::vector<DBCValueEntry>& entries) {
    while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t' || rest[pos] == ';')) ++pos;
    if (pos >= rest.size()) return false;

    std::size_t valStart = pos;
    while (pos < rest.size() && rest[pos] != ' ' && rest[pos] != '\t') ++pos;
    if (pos == valStart) return false;

    std::int64_t numVal = 0;
    {
        auto sv = std::string_view(rest.data() + valStart, pos - valStart);
        auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), numVal);
        if (ec != std::errc{}) return false;
    }

    while (pos < rest.size() && (rest[pos] == ' ' || rest[pos] == '\t')) ++pos;

    if (pos >= rest.size() || rest[pos] != '"') return false;
    ++pos;
    auto endQuote = rest.find('"', pos);
    if (endQuote == std::string::npos) return false;

    entries.push_back({numVal, rest.substr(pos, endQuote - pos)});
    pos = endQuote + 1;
    return true;
}

std::vector<DBCValueEntry> parseValueEntries(const std::string& rest) {
    std::vector<DBCValueEntry> entries;
    std::size_t pos = 0;

    while (pos < rest.size()) {
        // Parse one "<num> \"<label>\"" entry from `pos`. Each malformed-input
        // or end-of-input condition returns false so the loop has a single break;
        // on success the entry is appended and true is returned to continue.
        if (!parseOneValueEntry(rest, pos, entries)) {
            break;
        }
    }

    return entries;
}

DBCParseResult buildResult(
    std::vector<ParsedSignal>& signals,
    const std::vector<std::tuple<std::uint16_t, std::string, std::vector<DBCValueEntry>>>& valueTables
) {
    DBCParseResult result;

    // Apply value tables to matching signals
    for (const auto& [canId, signalName, entries] : valueTables) {
        for (auto& sig : signals) {
            if (sig.canId == canId && sig.name == signalName) {
                sig.valueTable = entries;
                break;
            }
        }
    }

    // Group signals by CAN ID
    for (auto& sig : signals) {
        result.signalsByCanId.try_emplace(sig.canId);
        result.signalsByCanId.at(sig.canId).emplace_back(DBCSignalParams{
            sig.canId,
            std::move(sig.name),
            sig.startBit,
            sig.bitLength,
            sig.byteOrder,
            sig.scale,
            sig.offset,
            sig.isSigned,
            std::move(sig.unit),
            sig.min,
            sig.max,
            std::move(sig.valueTable)
        });
    }

    return result;
}

/**
 * Parse a trailing uint16_t from a string starting at pos.
 *
 * Skips whitespace, extracts the next token, and parses it as uint16_t.
 * Advances pos to the first non-whitespace character after the parsed number.
 *
 * @param s      The string to parse
 * @param pos    Starting position (updated by reference)
 * @param result Output parameter for the parsed value
 * @return true if parsing succeeded, false otherwise
 */
[[nodiscard]] bool parseTrailingUint16(
    const std::string& s,
    std::size_t& pos,
    std::uint16_t& result
) noexcept {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;

    auto endOfId = pos;
    while (endOfId < s.size() && s[endOfId] != ' ' && s[endOfId] != '\t') ++endOfId;

    auto sv = std::string_view(s.data() + pos, endOfId - pos);
    auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);

    pos = endOfId;
    return ec == std::errc{};
}

/**
 * Parse a BO_ (message) line and extract the CAN ID.
 *
 * @param trimmed      Trimmed line content starting with "BO_"
 * @param currentCanId Output parameter for the parsed CAN ID
 * @return true if parsing succeeded, false otherwise
 */
[[nodiscard]] bool parseBoLine(
    const std::string& trimmed,
    std::uint16_t& currentCanId
) noexcept {
    auto pos = trimmed.find(' ', 3);
    if (pos == std::string::npos) return false;
    ++pos;

    std::uint16_t canId = 0;
    if (!parseTrailingUint16(trimmed, pos, canId)) return false;

    currentCanId = canId;
    return true;
}

/**
 * Parse a VAL_ (value table) line and extract its components.
 *
 * @param trimmed      Trimmed line content starting with "VAL_"
 * @param valueTables Output parameter for the parsed value table entries
 * @return true if parsing succeeded and produced entries, false otherwise
 */
[[nodiscard]] bool parseValLine(
    const std::string& trimmed,
    std::vector<std::tuple<std::uint16_t, std::string, std::vector<DBCValueEntry>>>& valueTables
) noexcept {
    auto pos = trimmed.find(' ', 4);
    if (pos == std::string::npos) return false;
    ++pos;

    std::uint16_t canId = 0;
    if (!parseTrailingUint16(trimmed, pos, canId)) return false;

    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) ++pos;

    auto endOfName = pos;
    while (endOfName < trimmed.size() && trimmed[endOfName] != ' ' && trimmed[endOfName] != '\t') ++endOfName;
    std::string signalName = trimmed.substr(pos, endOfName - pos);

    pos = endOfName;
    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) ++pos;

    auto entries = parseValueEntries(trimmed.substr(pos));
    if (!entries.empty()) {
        valueTables.emplace_back(canId, std::move(signalName), std::move(entries));
        return true;
    }
    return false;
}

} // anonymous namespace

DBCParseResult DBCFileParser::parseFile(
    const std::string& dbcFilePath
) const noexcept {
    try {
        std::ifstream file(dbcFilePath);
        if (!file.is_open()) return DBCParseResult{};

        std::stringstream buffer;
        buffer << file.rdbuf();
        return parseString(buffer.str());
    } catch (...) {
        return DBCParseResult{};
    }
}

DBCParseResult DBCFileParser::parseString(
    const std::string& dbcContent
) const noexcept {
    try {
        std::vector<ParsedSignal> signals;
        std::vector<std::tuple<std::uint16_t, std::string, std::vector<DBCValueEntry>>> valueTables;
        std::uint16_t currentCanId = 0;

        std::istringstream stream(dbcContent);
        std::string line;

        while (std::getline(stream, line)) {
            auto trimmed = trim(line);
            if (trimmed.empty()) continue;

            if (trimmed.rfind("BO_", 0) == 0) {
                if (!parseBoLine(trimmed, currentCanId)) continue;
            } else if (trimmed.rfind(" SG_", 0) == 0 || trimmed.rfind("SG_", 0) == 0) {
                if (currentCanId == 0) continue;

                ParsedSignal sig;
                if (parseSignalDefinition(trimmed, currentCanId, sig)) {
                    signals.push_back(std::move(sig));
                }
            } else if (trimmed.rfind("VAL_", 0) == 0) {
                if (!parseValLine(trimmed, valueTables)) continue;
            }
        }

        return buildResult(signals, valueTables);
    } catch (...) {
        return DBCParseResult{};
    }
}

bool DBCFileParser::canParse(
    const std::string& dbcFilePath
) const noexcept {
    try {
        std::ifstream file(dbcFilePath);
        if (!file.is_open()) return false;

        std::string firstLine;
        return static_cast<bool>(std::getline(file, firstLine)) && !firstLine.empty();
    } catch (...) {
        return false;
    }
}

} // namespace vehicle_sim::domain
