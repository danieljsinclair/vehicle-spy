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

bool parseSignalDefinition(
    const std::string& line,
    std::uint16_t currentCanId,
    ParsedSignal& out
) {
    auto pos = line.find("SG_");
    if (pos == std::string::npos) return false;
    pos += 3;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    std::size_t nameEnd = pos;
    while (nameEnd < line.size() && line[nameEnd] != ' ' && line[nameEnd] != ':') ++nameEnd;
    out.name = line.substr(pos, nameEnd - pos);
    pos = nameEnd;

    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos < line.size() && (line[pos] == 'M' || line[pos] == 'm')) {
        if (line[pos] == 'm') {
            ++pos;
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
        } else {
            ++pos;
        }
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    }

    if (pos >= line.size() || line[pos] != ':') return false;
    ++pos;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

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

    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    if (pos >= line.size() || line[pos] != '(') return false;
    ++pos;
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

    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    if (pos >= line.size() || line[pos] != '[') return false;
    ++pos;
    auto pipePos2 = line.find('|', pos);
    if (pipePos2 == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, pipePos2 - pos);
        out.min = std::stod(trim(sv));
    }
    pos = pipePos2 + 1;
    auto closeBracket = line.find(']', pos);
    if (closeBracket == std::string::npos) return false;
    {
        auto sv = std::string_view(line.data() + pos, closeBracket - pos);
        out.max = std::stod(trim(sv));
    }
    pos = closeBracket + 1;

    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    if (pos < line.size() && line[pos] == '"') {
        ++pos;
        auto endQuote = line.find('"', pos);
        if (endQuote != std::string::npos) {
            out.unit = line.substr(pos, endQuote - pos);
        }
    }

    out.canId = currentCanId;
    return true;
}

std::vector<DBCValueEntry> parseValueEntries(const std::string& rest) {
    std::vector<DBCValueEntry> entries;
    std::size_t pos = 0;

    while (pos < rest.size()) {
        // Parse one "<num> \"<label>\"" entry from `pos`. Each malformed-input
        // or end-of-input condition returns false so the loop has a single break;
        // on success the entry is appended and true is returned to continue.
        auto parseOne = [&]() {
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
        };

        if (!parseOne()) {
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
                auto pos = trimmed.find(' ', 3);
                if (pos == std::string::npos) continue;
                ++pos;
                auto endOfId = pos;
                while (endOfId < trimmed.size() && trimmed[endOfId] != ' ') ++endOfId;

                std::uint16_t canId = 0;
                auto sv = std::string_view(trimmed.data() + pos, endOfId - pos);
                auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), canId);
                if (ec != std::errc{}) continue;

                currentCanId = canId;
            } else if (trimmed.rfind(" SG_", 0) == 0 || trimmed.rfind("SG_", 0) == 0) {
                if (currentCanId == 0) continue;

                ParsedSignal sig;
                if (parseSignalDefinition(trimmed, currentCanId, sig)) {
                    signals.push_back(std::move(sig));
                }
            } else if (trimmed.rfind("VAL_", 0) == 0) {
                auto pos = trimmed.find(' ', 4);
                if (pos == std::string::npos) continue;
                ++pos;

                auto endOfId = pos;
                while (endOfId < trimmed.size() && trimmed[endOfId] != ' ') ++endOfId;
                std::uint16_t canId = 0;
                {
                    auto sv = std::string_view(trimmed.data() + pos, endOfId - pos);
                    auto [p, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), canId);
                    if (ec != std::errc{}) continue;
                }

                pos = endOfId;
                while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) ++pos;
                auto endOfName = pos;
                while (endOfName < trimmed.size() && trimmed[endOfName] != ' ' && trimmed[endOfName] != '\t') ++endOfName;
                std::string signalName = trimmed.substr(pos, endOfName - pos);

                pos = endOfName;
                while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) ++pos;

                auto entries = parseValueEntries(trimmed.substr(pos));
                if (!entries.empty()) {
                    valueTables.emplace_back(canId, std::move(signalName), std::move(entries));
                }
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
