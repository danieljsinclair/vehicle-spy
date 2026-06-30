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
constexpr std::uint32_t CAN_11BIT_MAX = 0x7FFu;   // 11-bit standard CAN ID ceiling
// constexpr std::uint32_t CAN_29BIT_MAX = 0x1FFFFFFFu; // future extended-frame support

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

// Mirror RawFrameNormaliser's rtrim so the two live normalisers agree on how
// trailing '\r'/'\n'/whitespace is tolerated (ELM327 terminates lines with '\r'
// and the transport may deliver it intact).
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
    auto res = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (res.ec != std::errc{}) return std::nullopt;
    return v;
}

// Tokenize on whitespace (spaces/tabs). Same definition as RawFrameNormaliser
// so a future shared helper could replace both without behaviour drift.
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

// ELM327 status/banner strings that appear in monitor mode and are NOT frames
// (and not errors). Compared case-insensitively against the whole trimmed line.
// Non-hex first tokens are already Skip via the isHex gate below; this explicit
// set documents the known ELM327 dialect noise and catches the '>' prompt.
bool isAdapterChatter(std::string_view trimmed) noexcept {
    if (trimmed == ">") return true;          // ready prompt
    const auto upper = toUpper(std::string(trimmed));
    static constexpr std::array<std::string_view, 14> kKnown = {
        "OK", "NO DATA", "DATA ERROR", "STOPPED", "?", "SEARCHING...",
        "SEARCHING", "ELM327", "UNABLE TO CONNECT", "BUS ERROR",
        "BUFFER FULL", "CAN ERROR", "BUS INIT", "ERROR",
    };
    // Version banners like "ELM327 v2.3" or "v1.5" start non-hex and are caught
    // by the isHex gate; nothing to do here.
    return std::any_of(std::begin(kKnown), std::end(kKnown),
                       [&](std::string_view kw) { return upper == kw; });
}

} // namespace

NormaliserResult Elm327Normaliser::parseMonitorLine(const std::string& line) noexcept {
    const auto trimmed = rtrim(line);

    // Blank / whitespace-only and the '>' ready-prompt are silently ignorable.
    if (isBlank(trimmed)) {
        return NormaliserResult::skip();
    }
    if (isAdapterChatter(trimmed)) {
        return NormaliserResult::skip();
    }

    auto tokens = tokenize(trimmed);
    if (tokens.empty()) {
        return NormaliserResult::skip();
    }

    // The first token must be a hex CAN-ID for this to be a frame. ELM327
    // monitor lines with ATH1 use a 3-hex-digit 11-bit ID. If the first token
    // isn't hex, this is banner / status text — Skip.
    if (!isHex(tokens[0])) {
        return NormaliserResult::skip();
    }

    // 11-bit CAN ID occupies up to 3 hex digits. More digits => not an 11-bit
    // monitor frame. (29-bit extended IDs are out of scope; a future change
    // can add an explicit extended-frame path here.)
    // MARKER(#18): detect & handle 29-bit extended CAN IDs when ATSP6/ATH1
    //              extended-frame output is exercised on real hardware.
    if (tokens[0].size() > 3) {
        return NormaliserResult::malformed();
    }

    // Frame-shaped: validate the data bytes. Zero data bytes is allowed; more
    // than 8 data bytes is Malformed.
    if (tokens.size() - 1 > CAN_PAYLOAD_BYTES) {
        return NormaliserResult::malformed();
    }
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        if (!isHex(tokens[t])) {
            return NormaliserResult::malformed();
        }
    }

    auto canId = parseHex(tokens[0]);
    if (!canId.has_value()) {
        return NormaliserResult::malformed();
    }
    if (*canId > CAN_11BIT_MAX) {
        return NormaliserResult::malformed();
    }

    domain::RawFrame frame;
    frame.timestampMs = 0;  // live monitor line — stamped on receipt by the decoder
    frame.canId = *canId;
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        auto byte = parseHex(tokens[t]);
        if (!byte.has_value() || *byte > 0xFFu) {
            return NormaliserResult::malformed();
        }
        frame.data[t - 1] = static_cast<std::uint8_t>(*byte);
    }
    frame.dlc = static_cast<std::uint8_t>(tokens.size() - 1);
    return NormaliserResult::ofFrame(std::move(frame));
}

NormaliserResult Elm327Normaliser::normalise(const std::string& line) noexcept {
    return parseMonitorLine(line);
}

} // namespace vehicle_sim::pipeline
