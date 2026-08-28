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

// True iff every char in the payload is a printable ASCII char that could
// appear in a CAN-frame line: hex digit, space, tab, comma, or one of a
// few harmless punctuation marks. Anything with a control byte, a NUL, or
// a high-bit character falls through to the binary path.
bool looksLikeAsciiLine(std::string_view payload) noexcept {
    if (payload.empty()) return false;
    for (unsigned char c : payload) {
        if (c < 0x20 || c > 0x7E) return false;
        if (!(isHexDigit(c) || isTokenSeparator(c) || c == 'x' || c == 'X')) return false;
    }
    return true;
}

std::optional<std::uint32_t> parseHexU32(std::string_view s) noexcept {
    std::uint32_t v = 0;
    if (auto r = std::from_chars(s.data(), s.data() + s.size(), v, 16); r.ec != std::errc{}) return std::nullopt;
    return v;
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
    while (i < payload.size()) {
        while (i < payload.size() && isTokenSeparator(static_cast<unsigned char>(payload[i]))) ++i;
        if (i >= payload.size()) break;
        std::size_t begin = i;
        while (i < payload.size() && !isTokenSeparator(static_cast<unsigned char>(payload[i]))) ++i;
        auto tok = payload.substr(begin, i - begin);
        auto v = parseHexU32(tok);
        if (!v.has_value()) return std::nullopt;
        if (!haveId) {
            if (tok.size() > 3) return std::nullopt;  // 11-bit CAN id is <= 3 hex digits
            if (*v > 0x7FFu) return std::nullopt;      // 11-bit range
            canId = *v;
            haveId = true;
        } else {
            if (tok.size() != 2) return std::nullopt;  // data bytes are exactly 2 hex chars
            if (*v > 0xFFu) return std::nullopt;
            if (data.size() >= 8) return std::nullopt;
            data.push_back(static_cast<std::uint8_t>(*v));
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
    if (payload.size() < 10) return std::nullopt;
    TwaiFrame f;
    f.timestampMs = tsMs;
    for (std::size_t k = 0; k < 10; ++k) f.bytes[k] = static_cast<std::uint8_t>(payload[k]);
    return f;
}

std::optional<TwaiFrame> BinaryFileSource::nextFrame() noexcept {
    if (!stream_.is_open() || stream_.eof() || stream_.bad()) return std::nullopt;

    std::string ts, payload;
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
