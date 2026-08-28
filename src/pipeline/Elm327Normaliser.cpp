#include "vehicle-sim/pipeline/Elm327Normaliser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vehicle_sim::pipeline {

namespace {

constexpr std::size_t CAN_PAYLOAD_BYTES = 8;
constexpr std::uint32_t CAN_11BIT_MAX = 0x7FFu;

bool isHex(std::string_view s) noexcept {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        const bool digit = u >= '0' && u <= '9';
        const bool lower = u >= 'a' && u <= 'f';
        const bool upper = u >= 'A' && u <= 'F';
        return digit || lower || upper;
    });
}

std::string_view rtrim(std::string_view s) noexcept {
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

bool isBlank(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return c == ' ' || c == '\r' || c == '\n' || c == '\t';
    });
}

std::optional<std::uint32_t> parseHex(std::string_view s) noexcept {
    std::uint32_t v = 0;
    if (auto res = std::from_chars(s.data(), s.data() + s.size(), v, 16); res.ec != std::errc{}) return std::nullopt;
    return v;
}

std::vector<std::string_view> tokenize(std::string_view s) noexcept {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size()) break;
        std::size_t begin = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
        tokens.push_back(s.substr(begin, i - begin));
    }
    return tokens;
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool isAdapterChatter(std::string_view trimmed) noexcept {
    if (trimmed == ">") return true;
    const auto upper = toUpper(std::string(trimmed));
    static constexpr std::array<std::string_view, 14> kKnown = {
        "OK", "NO DATA", "DATA ERROR", "STOPPED", "?", "SEARCHING...",
        "SEARCHING", "ELM327", "UNABLE TO CONNECT", "BUS ERROR",
        "BUFFER FULL", "CAN ERROR", "BUS INIT", "ERROR",
    };
    return std::any_of(std::begin(kKnown), std::end(kKnown),
                       [&](std::string_view kw) { return upper == kw; });
}

} // namespace

NormaliserResult Elm327Normaliser::parseMonitorLine(const std::string& line) noexcept {
    const auto trimmed = rtrim(line);
    if (isBlank(trimmed)) return NormaliserResult::skip();
    if (isAdapterChatter(trimmed)) return NormaliserResult::skip();

    auto tokens = tokenize(trimmed);
    if (tokens.empty()) return NormaliserResult::skip();
    if (!isHex(tokens[0])) return NormaliserResult::skip();
    if (tokens[0].size() > 3) return NormaliserResult::malformed();
    if (tokens.size() - 1 > CAN_PAYLOAD_BYTES) return NormaliserResult::malformed();
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        if (!isHex(tokens[t])) return NormaliserResult::malformed();
    }

    auto canId = parseHex(tokens[0]);
    if (!canId.has_value()) return NormaliserResult::malformed();
    if (*canId > CAN_11BIT_MAX) return NormaliserResult::malformed();

    TwaiFrame frame;
    frame.timestampMs = 0;
    frame.bytes[0] = static_cast<std::uint8_t>(*canId & 0xFF);
    frame.bytes[1] = static_cast<std::uint8_t>((*canId >> 8) & 0xFF);
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        auto byte = parseHex(tokens[t]);
        if (!byte.has_value() || *byte > 0xFFu) return NormaliserResult::malformed();
        frame.bytes[1 + t] = static_cast<std::uint8_t>(*byte);  // t=1 -> index 2 (data[0])
    }
    return NormaliserResult::ofFrame(std::move(frame));
}

NormaliserResult Elm327Normaliser::normalise(const std::string& line) noexcept {
    return parseMonitorLine(line);
}

} // namespace vehicle_sim::pipeline
