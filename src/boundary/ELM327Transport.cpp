#include "vehicle-sim/boundary/ELM327Transport.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace vehicle_sim::boundary {

// ================================================
// Helper Functions
// ================================================

namespace {
    const std::unordered_set<std::string> ERROR_MESSAGES = {
        "NO DATA", "ERROR", "STOPPED", "UNABLE TO CONNECT",
        "BUS ERROR", "BUFFER FULL", "CAN ERROR"
    };

    const std::unordered_set<std::string> INFO_MESSAGES = {
        "SEARCHING", "BUSINIT", "OK"
    };

    bool isErrorMessage(std::string_view str) {
        std::string upper{str};
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        return std::any_of(ERROR_MESSAGES.begin(), ERROR_MESSAGES.end(),
                           [&](std::string_view error) {
                               return upper.find(error) != std::string::npos;
                           });
    }

    std::uint8_t hexCharToByte(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        return 0xFF;
    }

    std::optional<std::uint8_t> parseHexByte(const std::string& hex) {
        if (hex.size() != 2) return std::nullopt;

        std::uint8_t high = hexCharToByte(hex[0]);
        std::uint8_t low = hexCharToByte(hex[1]);

        if (high == 0xFF || low == 0xFF) return std::nullopt;

        return (high << 4) | low;
    }

    // Strip the earliest informational banner (SEARCHING/BUSINIT/OK) and any
    // trailing separator chars so the data following it is what gets parsed.
    // Returns the substring after the earliest prefix, or the input unchanged
    // when no informational prefix is present.
    std::string stripInfoPrefixes(const std::string& cleaned) {
        std::size_t dataStart = std::string::npos;
        for (const auto& prefix : INFO_MESSAGES) {
            std::size_t pos = cleaned.find(prefix);
            if (pos != std::string::npos && (dataStart == std::string::npos || pos < dataStart)) {
                dataStart = pos + prefix.length();
                while (dataStart < cleaned.length() &&
                       (cleaned[dataStart] == '.' || cleaned[dataStart] == ':' ||
                        std::isspace(static_cast<unsigned char>(cleaned[dataStart])))) {
                    ++dataStart;
                }
            }
        }
        return (dataStart != std::string::npos) ? cleaned.substr(dataStart) : cleaned;
    }

    // Decode complete hex byte-pairs from `hexStr` into `out`, leaving any
    // trailing lone digit in `hexStr`. `hexStr` holds only isxdigit characters
    // by construction (the accumulator only appends those), so every parseHexByte
    // succeeds — the assert documents that invariant.
    void flushHexAccumulator(std::string& hexStr, std::vector<std::uint8_t>& out) {
        while (hexStr.length() >= 2) {
            auto byte = parseHexByte(hexStr.substr(0, 2));
            assert(byte.has_value() && "hexStr contains only isxdigit characters");
            out.push_back(*byte);
            hexStr = hexStr.substr(2);
        }
    }

    // ELM327 status/error strings that are not CAN frames. `upper` must already
    // be upper-cased; an exact (whole-line) match identifies a status response.
    bool isStatusMessage(std::string_view upper) {
        static constexpr std::string_view STATUSES[] = {
            "OK", "NO DATA", "ERROR", "STOPPED", "UNABLE TO CONNECT",
            "BUS ERROR", "BUFFER FULL", "CAN ERROR", "BUS INIT", "SEARCHING"
        };
        for (auto status : STATUSES) {
            if (upper == status) return true;
        }
        return false;
    }

    // Split `cleaned` into upper-cased hex tokens, treating any run of non-hex
    // characters as a separator. Empty tokens (leading/consecutive separators)
    // are dropped.
    std::vector<std::string> tokenizeHex(const std::string& cleaned) {
        std::vector<std::string> tokens;
        std::string current;
        for (char c : cleaned) {
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                current += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            } else if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }
        return tokens;
    }

    // For a 10-token frame, classify the leading token: a monitor-mode type
    // prefix in the 0x600-0x6FF range returns offset 1 (data starts at token 1);
    // anything else is malformed and returns nullopt. Frames with != 10 tokens
    // have no type prefix, so offset 0 is returned.
    std::optional<std::size_t> classifyTypePrefix(const std::vector<std::string>& tokens) {
        if (tokens.size() != 10) return 0;

        unsigned int type = 0;
        if (sscanf(tokens[0].c_str(), "%X", &type) != 1) return std::nullopt;
        if (type >= 0x600 && type <= 0x6FF) return 1;
        return std::nullopt;
    }
}

// ================================================
// OBD2 Query Building
// ================================================

std::string ELM327Transport::buildOBD2Query(uint8_t mode, uint8_t pid) {
    std::array<char, 16> buffer;
    snprintf(buffer.data(), buffer.size(), "%02X %02X\r", mode, pid);
    return std::string(buffer.data());
}

// ================================================
// OBD2 Response Parsing
// ================================================

std::optional<std::vector<uint8_t>> ELM327Transport::parseOBD2Response(const std::string& response) {
    if (response.empty()) return std::nullopt;

    std::string cleaned = extractPrompt(response);

    if (cleaned.empty() || cleaned == ">") return std::nullopt;

    cleaned = stripInfoPrefixes(cleaned);

    if (cleaned.empty()) return std::nullopt;

    if (isErrorMessage(cleaned)) return std::nullopt;

    std::vector<uint8_t> result;
    std::string hexStr;

    for (char c : cleaned) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            hexStr += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            continue;
        }

        // A ':' marks a multi-frame line-number segment (e.g. "014:"): the
        // digits accumulated before it must be discarded, not parsed as data.
        if (c == ':') {
            hexStr.clear();
            continue;
        }

        // Any other non-hex char is a separator: flush the accumulated pairs.
        flushHexAccumulator(hexStr, result);
    }

    flushHexAccumulator(hexStr, result);

    // A trailing lone digit (odd count overall) cannot form a byte -> invalid.
    if (!hexStr.empty()) return std::nullopt;

    if (result.empty()) return std::nullopt;

    return result;
}

// ================================================
// AT Command Building
// ================================================

std::string ELM327Transport::buildATCommand(const std::string& command) {
    return "AT" + command + "\r";
}

// ================================================
// AT Response Parsing
// ================================================

bool ELM327Transport::parseATResponse(const std::string& response) {
    if (response.empty()) return false;

    std::string cleaned = extractPrompt(response);

    if (cleaned.find("ERROR") != std::string::npos ||
        cleaned.find("?") != std::string::npos) {
        return false;
    }

    return cleaned.find("OK") != std::string::npos;
}

// ================================================
// Initialization Sequence
// ================================================

std::vector<ATCommand> ELM327Transport::buildInitSequence() {
    return {
        {"ATZ\r", 500},     // Full reset - needs long delay
        {"ATE0\r", 50},    // Echo off
        {"ATH0\r", 50},    // Headers off
        {"ATL0\r", 50},    // Linefeeds off
        {"ATSP0\r", 50},   // Auto protocol detection
        {"ATS0\r", 50},    // Spaces off
        {"ATSTFF\r", 50}   // Maximum timeout
    };
}

std::vector<ATCommand> ELM327Transport::buildVINQueryInitSequence() {
    return {
        {"ATZ\r", 3000},    // Full reset - ELM327 can take 2+ seconds
        {"ATE0\r", 100},    // Echo off
        {"ATH0\r", 100},    // Headers off
        {"ATL0\r", 100},    // Linefeeds off
        {"ATSP6\r", 100},   // ISO 15765-4 CAN 500kbps 11-bit (no probing)
        {"ATS0\r", 100},    // Spaces off
        {"ATSTFF\r", 100}   // Maximum timeout
    };
}

// ================================================
// Prompt Extraction
// ================================================

std::string ELM327Transport::extractPrompt(const std::string& response) {
    if (size_t promptPos = response.find('>'); promptPos != std::string::npos) {
        return response.substr(0, promptPos);
    }
    return response;
}

// ================================================
// CAN Monitor Mode Support
// ================================================

std::vector<ATCommand> ELM327Transport::buildCANMonitorInitSequence() {
    return {
        {"ATZ\r", 500},     // Full reset - needs long delay
        {"ATE0\r", 50},    // Echo off
        {"ATSP6\r", 50},   // ISO 15765-4 CAN 500kbps 11-bit (Tesla)
        {"ATH1\r", 50},    // Headers on (need CAN ID in response)
        {"ATMA\r", 50}     // Monitor all CAN frames
    };
}

std::string ELM327Transport::buildCANFilter(uint16_t canId) {
    std::array<char, 32> buffer;
    snprintf(buffer.data(), buffer.size(), "ATCRA%X\r", canId);
    return std::string(buffer.data());
}

std::optional<CANFrame> ELM327Transport::parseCANFrame(const std::string& line) {
    if (line.empty()) return std::nullopt;

    // Remove trailing line endings
    std::string cleaned = line;
    while (!cleaned.empty() && (cleaned.back() == '\r' || cleaned.back() == '\n')) {
        cleaned.pop_back();
    }

    // Check for prompt only
    if (cleaned == ">") return std::nullopt;

    // Check for ELM327 responses (not CAN frames)
    std::string upper = cleaned;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (isStatusMessage(upper)) return std::nullopt;

    // Check for OBD2 multi-frame response with line numbers (e.g., "014:")
    if (cleaned.find(':') != std::string::npos) return std::nullopt;

    // A valid CAN frame is CAN ID + 8 data bytes (9 tokens), or a monitor-mode
    // type prefix + CAN ID + 8 data bytes (10 tokens). Anything else is an OBD2
    // response with fewer bytes.
    auto tokens = tokenizeHex(cleaned);
    if (tokens.size() < 9 || tokens.size() > 10) return std::nullopt;

    auto dataStart = classifyTypePrefix(tokens);
    if (!dataStart.has_value()) return std::nullopt;

    // Parse CAN ID (first token after the optional type prefix)
    unsigned int id = 0;
    if (sscanf(tokens[*dataStart].c_str(), "%X", &id) != 1) return std::nullopt;

    // Verify we have exactly 8 data bytes remaining
    if (tokens.size() - *dataStart - 1 != 8) return std::nullopt;

    // Parse data bytes
    std::vector<uint8_t> data;
    for (std::size_t i = *dataStart + 1; i < tokens.size(); ++i) {
        if (tokens[i].size() != 2) return std::nullopt;
        auto byte = parseHexByte(tokens[i]);
        if (!byte.has_value()) return std::nullopt;
        data.push_back(*byte);
    }

    CANFrame frame;
    frame.canId = static_cast<uint16_t>(id);
    frame.data = std::move(data);
    return frame;
}

} // namespace vehicle_sim::boundary
