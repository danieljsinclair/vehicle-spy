#include "vehicle-sim/pipeline/RawFrameNormaliser.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vehicle_sim::pipeline {

namespace {

constexpr std::size_t CAN_PAYLOAD_BYTES = 8;

bool isHex(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        const bool digit = u >= '0' && u <= '9';
        const bool lower = u >= 'a' && u <= 'f';
        const bool upper = u >= 'A' && u <= 'F';
        if (!(digit || lower || upper)) return false;
    }
    return true;
}

std::string_view rtrim(std::string_view s) noexcept {
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

bool isBlank(std::string_view s) noexcept {
    for (char c : s) {
        if (c != ' ' && c != '\r' && c != '\n' && c != '\t') return false;
    }
    return true;
}

std::optional<std::uint32_t> parseHex(std::string_view s) noexcept {
    std::uint32_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (res.ec != std::errc{}) return std::nullopt;
    return v;
}

// Tokenize on whitespace (spaces/tabs). Mirrors the verbatim tokenizer in
// CaptureLog so the two normalisers agree on what a "token" is.
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

} // namespace

NormaliserResult RawFrameNormaliser::parseLiveLine(const std::string& line) noexcept {
    const auto trimmed = rtrim(line);

    // Blank / whitespace-only and '#' header lines are silently ignorable.
    if (isBlank(trimmed)) {
        return NormaliserResult::skip();
    }
    if (!trimmed.empty() && trimmed[0] == '#') {
        return NormaliserResult::skip();
    }

    auto tokens = tokenize(trimmed);
    if (tokens.empty()) {
        return NormaliserResult::skip();
    }

    // The first token must be a hex CAN-ID for this to be a frame. If it
    // isn't hex, this is status text / a banner / a prompt — Skip.
    if (!isHex(tokens[0])) {
        return NormaliserResult::skip();
    }

    // Frame-shaped: validate the rest. Zero data bytes is allowed (some
    // adapters emit remote frames); more than 8 data bytes is Malformed.
    if (tokens.size() - 1 > CAN_PAYLOAD_BYTES) {
        return NormaliserResult::malformed();
    }
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        if (!isHex(tokens[t])) {
            return NormaliserResult::malformed();
        }
    }

    auto canId = parseHex(tokens[0]);
    if (!canId) {
        return NormaliserResult::malformed();
    }

    domain::RawFrame frame;
    frame.timestampMs = 0;  // live — stamped on receipt by the decoder/transport
    frame.canId = *canId;
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        auto byte = parseHex(tokens[t]);
        if (!byte || *byte > 0xFFu) {
            return NormaliserResult::malformed();
        }
        frame.data[t - 1] = static_cast<std::uint8_t>(*byte);
    }
    frame.dlc = static_cast<std::uint8_t>(tokens.size() - 1);
    return NormaliserResult::ofFrame(std::move(frame), /*timestamped=*/false);
}

NormaliserResult RawFrameNormaliser::normalise(const std::string& line) noexcept {
    return parseLiveLine(line);
}

} // namespace vehicle_sim::pipeline
