#include "vehicle-sim/boundary/ELM327Transport.h"

#include <sstream>
#include <algorithm>
#include <cctype>
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

    bool isInfoMessage(const std::string& str) {
        std::string upper = str;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        for (const auto& info : INFO_MESSAGES) {
            if (upper.find(info) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    bool isErrorMessage(const std::string& str) {
        std::string upper = str;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        for (const auto& error : ERROR_MESSAGES) {
            if (upper.find(error) != std::string::npos) {
                return true;
            }
        }
        return false;
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
}

// ================================================
// OBD2 Query Building
// ================================================

std::string ELM327Transport::buildOBD2Query(uint8_t mode, uint8_t pid) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02X %02X\r", mode, pid);
    return std::string(buffer);
}

// ================================================
// OBD2 Response Parsing
// ================================================

std::optional<std::vector<uint8_t>> ELM327Transport::parseOBD2Response(const std::string& response) {
    if (response.empty()) return std::nullopt;

    std::string cleaned = extractPrompt(response);

    if (cleaned.empty() || cleaned == ">") return std::nullopt;

    // Remove informational prefixes like "SEARCHING..." but keep the data
    std::size_t dataStart = std::string::npos;
    for (const auto& prefix : INFO_MESSAGES) {
        std::size_t pos = cleaned.find(prefix);
        if (pos != std::string::npos && (dataStart == std::string::npos || pos < dataStart)) {
            dataStart = pos + prefix.length();
            // Skip common trailing chars after info messages
            while (dataStart < cleaned.length() && (cleaned[dataStart] == '.' || cleaned[dataStart] == ':' || std::isspace(cleaned[dataStart]))) {
                dataStart++;
            }
        }
    }

    if (dataStart != std::string::npos) {
        cleaned = cleaned.substr(dataStart);
    }

    if (cleaned.empty()) return std::nullopt;

    if (isErrorMessage(cleaned)) return std::nullopt;

    std::vector<uint8_t> result;
    std::string hexStr;

    for (char c : cleaned) {
        if (std::isxdigit(c)) {
            hexStr += static_cast<char>(std::toupper(c));
        } else {
            // Check for line numbers like "014:" - reset hexStr if we hit ':' and have accumulated digits
            // This prevents line numbers from being parsed as data
            if (c == ':') {
                hexStr.clear();
                continue;
            }

            // Process accumulated hex when we hit a non-hex character
            while (hexStr.length() >= 2) {
                auto byte = parseHexByte(hexStr.substr(0, 2));
                if (byte) {
                    result.push_back(*byte);
                } else {
                    return std::nullopt; // Invalid hex found
                }
                hexStr = hexStr.substr(2);
            }
        }
    }

    // Process remaining hex
    while (hexStr.length() >= 2) {
        auto byte = parseHexByte(hexStr.substr(0, 2));
        if (byte) {
            result.push_back(*byte);
        } else {
            return std::nullopt;
        }
        hexStr = hexStr.substr(2);
    }

    // If there's an odd number of hex digits remaining, it's invalid
    if (!hexStr.empty()) {
        return std::nullopt;
    }

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
    size_t promptPos = response.find('>');
    if (promptPos != std::string::npos) {
        return response.substr(0, promptPos);
    }
    return response;
}

// ================================================
// CAN Monitor Mode Support
// ================================================

std::vector<ATCommand> ELM327Transport::buildCANMonitorInitSequence() {
    return {
        {"ATZ\r", 3000},    // Full reset - ELM327 can take 2+ seconds
        {"ATE0\r", 100},    // Echo off
        {"ATL0\r", 100},    // Linefeeds off
        {"ATSP6\r", 100},   // ISO 15765-4 CAN 500kbps 11-bit (Tesla)
        {"ATH1\r", 100},    // Headers on (need CAN ID in response)
        {"ATCSM1\r", 100},  // Silent monitoring - no CAN ACK bits on bus
        {"ATMA\r", 100}     // Monitor all CAN frames
    };
}

std::string ELM327Transport::buildCANFilter(uint16_t canId) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "ATCRA%X\r", canId);
    return std::string(buffer);
}

std::optional<CANFrame> ELM327Transport::parseCANFrame(const std::string& line) {
    if (line.empty()) {
        return std::nullopt;
    }

    // Remove trailing line endings
    std::string cleaned = line;
    while (!cleaned.empty() && (cleaned.back() == '\r' || cleaned.back() == '\n')) {
        cleaned.pop_back();
    }

    // Check for prompt only
    if (cleaned == ">") {
        return std::nullopt;
    }

    // Check for ELM327 responses (not CAN frames)
    std::string upper = cleaned;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "OK" || upper == "NO DATA" || upper == "ERROR" || upper == "STOPPED" ||
        upper == "UNABLE TO CONNECT" || upper == "BUS ERROR" || upper == "BUFFER FULL" ||
        upper == "CAN ERROR" || upper == "BUS INIT" || upper == "SEARCHING") {
        return std::nullopt;
    }

    // Check for OBD2 multi-frame response with line numbers (e.g., "014:")
    if (cleaned.find(':') != std::string::npos) {
        return std::nullopt;
    }

    // Check for OBD2 response format (starts with 0x41 which is mode 01 response)
    // A valid CAN frame should have more hex bytes than a typical OBD2 response
    std::vector<std::string> tokens;
    std::string current;
    for (char c : cleaned) {
        if (std::isxdigit(c)) {
            current += static_cast<char>(std::toupper(c));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    // Need at least CAN ID + 8 data bytes (9 tokens minimum)
    // Or CAN ID + type byte + 8 data bytes (10 tokens with type prefix)
    if (tokens.size() < 9 || tokens.size() > 10) {
        // Could be an OBD2 response with fewer bytes
        return std::nullopt;
    }

    size_t dataStart = 0;
    uint16_t canId = 0;

    // Check if first token is a type byte (0x600-0x6FF range)
    if (tokens.size() == 10) {
        unsigned int type;
        if (sscanf(tokens[0].c_str(), "%X", &type) != 1) {
            return std::nullopt;
        }
        // Type byte in range 0x600-0x6FF indicates monitor mode type prefix
        if (type >= 0x600 && type <= 0x6FF) {
            dataStart = 1;
        } else {
            // Not a valid type prefix, might be malformed
            return std::nullopt;
        }
    }

    // Parse CAN ID (first token after optional type)
    unsigned int id;
    if (sscanf(tokens[dataStart].c_str(), "%X", &id) != 1) {
        return std::nullopt;
    }
    canId = static_cast<uint16_t>(id);

    // Verify we have exactly 8 data bytes remaining
    size_t dataCount = tokens.size() - dataStart - 1;
    if (dataCount != 8) {
        return std::nullopt;
    }

    // Parse data bytes
    std::vector<uint8_t> data;
    for (size_t i = dataStart + 1; i < tokens.size(); ++i) {
        if (tokens[i].size() != 2) {
            return std::nullopt;
        }
        auto byte = parseHexByte(tokens[i]);
        if (!byte) {
            return std::nullopt;
        }
        data.push_back(*byte);
    }

    CANFrame frame;
    frame.canId = canId;
    frame.data = std::move(data);
    return frame;
}

} // namespace vehicle_sim::boundary
