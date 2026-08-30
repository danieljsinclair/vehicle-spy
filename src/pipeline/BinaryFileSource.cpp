#include "vehicle-sim/pipeline/BinaryFileSource.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string_view>

namespace vehicle_sim::pipeline {

namespace {

bool isHexDigit(unsigned char c) noexcept {
    return (c >= '0' && c <= '9')
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

bool isTokenSeparator(unsigned char c) noexcept {
    return c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n';
}

// One allowed ASCII char in a CAN-frame line: a printable byte that is a
// hex digit, a token separator, or the '0x' prefix characters. Anything
// else (control bytes, NUL, high-bit, letters beyond x/X, underscores,
// punctuation) fails so that status text like "TWAI started" or headers
// like "raw_line" fall through to the binary path (where they fail cleanly)
// instead of being mis-tokenised as CAN frames.
bool isAllowedAsciiLineChar(unsigned char c) noexcept {
    if (c < 0x20 || c > 0x7E) return false;
    return isHexDigit(c) || c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n'
           || c == 'x' || c == 'X';
}

// True iff every char in the payload is a printable ASCII char that could
// appear in a CAN-frame line: hex digit, space, tab, comma, or one of a
// few harmless punctuation marks. Anything with a control byte, a NUL, or
// a high-bit character falls through to the binary path.
bool looksLikeAsciiLine(std::string_view payload) noexcept {
    if (payload.empty()) return false;
    return std::all_of(payload.begin(), payload.end(),
                       [](char c) { return isAllowedAsciiLineChar(static_cast<unsigned char>(c)); });
}

std::optional<std::uint32_t> parseHexU32(std::string_view s) noexcept {
    std::uint32_t v = 0;
    if (auto r = std::from_chars(s.data(), s.data() + s.size(), v, 16); r.ec != std::errc{}) return std::nullopt;
    return v;
}

// Advance `i` past separators and return the next token (empty when the
// remainder is all separators). The single mutation point for the scan
// index, so the tokenise loop stays flat (cpp:S3776).
std::string_view nextToken(std::string_view s, std::size_t& i) noexcept {
    while (i < s.size() && isTokenSeparator(static_cast<unsigned char>(s[i]))) ++i;
    const std::size_t begin = i;
    while (i < s.size() && !isTokenSeparator(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(begin, i - begin);
}

// First token of a frame line = 11-bit CAN id: at most 3 hex digits and
// within the 11-bit range.
bool parseCanIdToken(std::string_view tok, std::uint32_t v,
                     std::uint32_t& canIdOut) noexcept {
    if (tok.size() > 3) return false;  // 11-bit CAN id is <= 3 hex digits
    if (v > 0x7FFu) return false;      // 11-bit range
    canIdOut = v;
    return true;
}

// A data-byte token: exactly 2 hex chars, <= 0xFF, at most 8 bytes.
bool appendDataByteToken(std::string_view tok, std::uint32_t v,
                         std::vector<std::uint8_t>& data) noexcept {
    if (tok.size() != 2) return false;  // data bytes are exactly 2 hex chars
    if (v > 0xFFu) return false;
    if (data.size() >= 8) return false;
    data.push_back(static_cast<std::uint8_t>(v));
    return true;
}

} // namespace

bool BinaryFileSource::open() noexcept {
    if (stream_.is_open()) return true;
    stream_.open(filePath_, std::ios::binary);
    return stream_.is_open();
}

bool BinaryFileSource::isOpen() const noexcept {
    return stream_.is_open() && !stream_.eof() && !stream_.bad();
}

bool BinaryFileSource::readNext(std::string& tsOut, std::string& payloadOut) {
    std::string line;
    while (std::getline(stream_, line)) {
        // Strip trailing \r (CRLF files).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        if (comma == 0) continue;

        // Timestamp must be a plain decimal (text-mode) for both ASCII and
        // binary rows. The binary part starts AFTER the comma.
        const auto tsField = std::string_view(line).substr(0, comma);
        std::uint64_t ts = 0;
        if (auto r = std::from_chars(tsField.data(), tsField.data() + tsField.size(), ts, 10);
            r.ec != std::errc{}) continue;

        tsOut.assign(tsField);
        payloadOut.assign(line, comma + 1);
        return true;
    }
    return false;
}

std::optional<TwaiFrame> BinaryFileSource::parseAscii(
    std::uint64_t tsMs, std::string_view payload) const {
    // Tokenise on whitespace/commas. First token = CAN-ID, rest = data bytes.
    std::vector<std::uint8_t> data;
    data.reserve(8);
    std::uint32_t canId = 0;
    bool haveId = false;
    std::size_t i = 0;
    for (std::string_view tok = nextToken(payload, i); !tok.empty();
         tok = nextToken(payload, i)) {
        const auto v = parseHexU32(tok);
        if (!v.has_value()) return std::nullopt;
        if (!haveId) {
            if (!parseCanIdToken(tok, *v, canId)) return std::nullopt;
            haveId = true;
        } else if (!appendDataByteToken(tok, *v, data)) {
            return std::nullopt;
        }
    }
    if (!haveId) return std::nullopt;

    TwaiFrame f;
    f.timestampMs = tsMs;
    f.bytes[0] = static_cast<std::uint8_t>(canId & 0xFF);
    f.bytes[1] = static_cast<std::uint8_t>((canId >> 8) & 0xFF);
    for (std::size_t k = 0; k < data.size(); ++k) f.bytes[2 + k] = data[k];
    return f;
}

std::optional<TwaiFrame> BinaryFileSource::parseBinary(
    std::uint64_t tsMs, std::string_view payload) const {
    // Accept exactly 10 bytes — the TWAI frame layout. Reject anything
    // longer (status text, headers, noise) so it is counted as skipped
    // rather than decoded as a garbage frame.
    if (payload.size() != 10) return std::nullopt;
    TwaiFrame f;
    f.timestampMs = tsMs;
    for (std::size_t k = 0; k < 10; ++k) f.bytes[k] = static_cast<std::uint8_t>(payload[k]);
    return f;
}

std::optional<TwaiFrame> BinaryFileSource::nextFrame() noexcept {
    if (!stream_.is_open() || stream_.eof() || stream_.bad()) return std::nullopt;

    std::string ts;
    std::string payload;
    while (readNext(ts, payload)) {
        std::uint64_t tsMs = 0;
        std::from_chars(ts.data(), ts.data() + ts.size(), tsMs, 10);

        // Sniff: ASCII iff the payload looks like a line of hex tokens
        // (printable hex digits, whitespace, or commas). Anything else
        // (control bytes, non-printable, binary) is treated as the raw 10
        // bytes of a TWAI frame.
        if (looksLikeAsciiLine(payload)) {
            if (auto f = parseAscii(tsMs, payload)) return f;
        } else if (auto f = parseBinary(tsMs, payload)) {
            return f;
        }
        // Else: line was neither a valid ASCII nor a valid binary frame —
        // skip silently and try the next record.
    }
    return std::nullopt;
}

} // namespace vehicle_sim::pipeline
