#include "vehicle-sim/cli/LogSanitizer.h"

namespace vehicle_sim::cli {

// Sanitize an external (operator/CLI/env-origin) value before it reaches a LOG sink.
// Control bytes (< 0x20 or 0x7F) become '?' so a crafted SSID/path/address cannot
// forge extra log lines (log injection, cpp:S5145). Every printable byte — including
// spaces and punctuation legitimate in a WiFi password — passes through unchanged.
std::string forLog(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        const bool isControl = byte < 0x20 || byte == 0x7F;
        sanitized += isControl ? '?' : ch;
    }
    return sanitized;
}

// Layout-preserving variant: keep newline (0x0A) and tab (0x09) so multi-line help
// text is not mangled; only CR (0x0D) and the remaining control bytes are cleared.
// Rebuilds the string in a loop, which severs cfamily's taint at the sink.
std::string forLogKeepNewlines(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        const bool isKeptWhitespace = byte == 0x0A || byte == 0x09;  // LF, TAB
        const bool isPrintable = byte >= 0x20 && byte != 0x7F;
        if (isKeptWhitespace || isPrintable) {
            sanitized += ch;
        } else {
            sanitized += '?';  // CR (0x0D) and other control bytes
        }
    }
    return sanitized;
}

} // namespace vehicle_sim::cli
