#include "vehicle-sim/domain/CaptureLog.h"

#include <charconv>
#include <fstream>

namespace vehicle_sim::domain {

namespace {

constexpr std::size_t CAN_PAYLOAD_BYTES = 8;

std::string_view rtrim(std::string_view s) noexcept {
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<std::uint64_t> parseDecimal(std::string_view s) noexcept {
    std::uint64_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v, 10);
    if (res.ec != std::errc{}) return std::nullopt;
    return v;
}

std::optional<std::uint32_t> parseHex(std::string_view s) noexcept {
    std::uint32_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (res.ec != std::errc{}) return std::nullopt;
    return v;
}

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

bool isBlank(std::string_view s) noexcept {
    for (char c : s) {
        if (c != ' ' && c != '\r' && c != '\n' && c != '\t') return false;
    }
    return true;
}

// Parse a legacy CSV row "timestampMs,canId,dlc,dataHex" (the caller has
// already split off the timestamp). Returns Frame or Malformed; never
// NotAFrame (this path is only reached when the line is frame-shaped).
CaptureLineParse parseLegacyCsv(std::string_view tsField,
                                std::string_view restFields) noexcept {
    std::string_view fields[4];
    fields[0] = tsField;
    // Re-split restFields ("canId,dlc,dataHex") into fields[1..3].
    std::string_view rest[3];
    int n = 0;
    std::size_t start = 0;
    bool tooMany = false;
    for (std::size_t i = 0; i <= restFields.size(); ++i) {
        if (i == restFields.size() || restFields[i] == ',') {
            if (n >= 3) { tooMany = true; break; }
            rest[n++] = restFields.substr(start, i - start);
            start = i + 1;
        }
    }
    CaptureLineParse out;
    if (tooMany || n != 3) {
        out.result = CaptureParseResult::Malformed;
        return out;
    }
    fields[1] = rest[0];
    fields[2] = rest[1];
    fields[3] = rest[2];

    auto ts = parseDecimal(fields[0]);
    auto canId = parseHex(fields[1]);
    auto dlc = parseDecimal(fields[2]);
    if (!ts.has_value() || !canId.has_value() || !dlc.has_value() || *dlc > CAN_PAYLOAD_BYTES) {
        out.result = CaptureParseResult::Malformed;
        return out;
    }

    const auto hex = fields[3];
    if (hex.size() % 2 != 0 || hex.size() / 2 > CAN_PAYLOAD_BYTES) {
        out.result = CaptureParseResult::Malformed;
        return out;
    }
    RawFrame frame;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto byte = parseHex(hex.substr(i, 2));
        if (!byte.has_value()) {
            out.result = CaptureParseResult::Malformed;
            return out;
        }
        frame.data[i / 2] = static_cast<std::uint8_t>(*byte);
    }
    frame.timestampMs = *ts;
    frame.canId = *canId;
    frame.dlc = static_cast<std::uint8_t>(*dlc);
    out.result = CaptureParseResult::Frame;
    out.frame = frame;
    return out;
}

// Parse a verbatim notepad row "timestampMs,<CANID> <D0> ... <D7>" (the caller
// has already split off the timestamp). Tokens are whitespace-separated hex
// values; the first is the CAN-ID (hex), the rest are up to 8 data bytes.
// Returns Frame/Malformed on frame-shaped input; NotAFrame if the token after
// the comma isn't hex (status text / escaped noise / header).
CaptureLineParse parseVerbatim(std::string_view tsField,
                               std::string_view payload) noexcept {
    CaptureLineParse out;
    auto ts = parseDecimal(tsField);
    if (!ts.has_value()) {
        out.result = CaptureParseResult::Malformed;
        return out;
    }

    // Tokenize the payload on whitespace.
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < payload.size()) {
        while (i < payload.size() && (payload[i] == ' ' || payload[i] == '\t')) ++i;
        if (i >= payload.size()) break;
        std::size_t begin = i;
        while (i < payload.size() && payload[i] != ' ' && payload[i] != '\t') ++i;
        tokens.push_back(payload.substr(begin, i - begin));
    }

    if (tokens.empty()) {
        // "ts," with nothing after the comma (e.g. legacy zero-dlc is caught
        // by the CSV path). Treat an empty verbatim payload as NotAFrame.
        out.result = CaptureParseResult::NotAFrame;
        return out;
    }
    // The first token must be a hex CAN-ID for this to be a frame. If it
    // isn't hex, this is status text / escaped noise — NotAFrame.
    if (!isHex(tokens[0])) {
        out.result = CaptureParseResult::NotAFrame;
        return out;
    }
    // Frame-shaped: validate the rest. More than 8 data tokens, or any
    // non-hex token, is Malformed (looks like a frame but is broken).
    if (tokens.size() - 1 > CAN_PAYLOAD_BYTES) {
        out.result = CaptureParseResult::Malformed;
        return out;
    }
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        if (!isHex(tokens[t])) {
            out.result = CaptureParseResult::Malformed;
            return out;
        }
    }

    RawFrame frame;
    frame.timestampMs = *ts;
    auto canId = parseHex(tokens[0]);
    if (!canId.has_value()) {
        out.result = CaptureParseResult::Malformed;
        return out;
    }
    frame.canId = *canId;
    for (std::size_t t = 1; t < tokens.size(); ++t) {
        auto byte = parseHex(tokens[t]);
        if (!byte.has_value() || *byte > 0xFFu) {
            out.result = CaptureParseResult::Malformed;
            return out;
        }
        frame.data[t - 1] = static_cast<std::uint8_t>(*byte);
    }
    frame.dlc = static_cast<std::uint8_t>(tokens.size() - 1);
    out.result = CaptureParseResult::Frame;
    out.frame = frame;
    return out;
}

} // namespace

CaptureLineParse parseCaptureLine(std::string_view line) noexcept {
    CaptureLineParse out;
    const auto trimmed = rtrim(line);

    // Blank / whitespace-only and the '#' header are silently ignorable.
    if (isBlank(trimmed)) {
        out.result = CaptureParseResult::NotAFrame;
        return out;
    }
    if (!trimmed.empty() && trimmed[0] == '#') {
        out.result = CaptureParseResult::NotAFrame;
        return out;
    }

    // Split on the FIRST comma → [ts, rest]. No comma at all means it can't
    // be either supported format → NotAFrame.
    const auto comma = trimmed.find(',');
    if (comma == std::string_view::npos) {
        out.result = CaptureParseResult::NotAFrame;
        return out;
    }
    const auto tsField = trimmed.substr(0, comma);
    const auto rest = trimmed.substr(comma + 1);

    // The legacy CSV header ("timestamp_ms,can_id,dlc,data_hex") is not a
    // frame and not an error — ignore it silently like the '#' notepad header.
    if (tsField == "timestamp_ms") {
        out.result = CaptureParseResult::NotAFrame;
        return out;
    }

    // Legacy CSV has 3 more commas (id,dlc,hex); verbatim has none. Detect by
    // whether there is another comma anywhere in `rest`.
    if (rest.find(',') != std::string_view::npos) {
        return parseLegacyCsv(tsField, rest);
    }
    return parseVerbatim(tsField, rest);
}

std::vector<std::uint8_t> toTwaiFrame(const RawFrame& frame) noexcept {
    std::vector<std::uint8_t> out(2 + CAN_PAYLOAD_BYTES);
    out[0] = static_cast<std::uint8_t>(frame.canId & 0xFFu);
    out[1] = static_cast<std::uint8_t>((frame.canId >> 8) & 0xFFu);
    for (std::size_t i = 0; i < CAN_PAYLOAD_BYTES; ++i) {
        out[2 + i] = frame.data[i];
    }
    return out;
}

CaptureFile loadCaptureFile(const std::string& path) noexcept {
    CaptureFile result;
    std::ifstream in(path);
    if (!in.is_open()) {
        result.opened = false;
        return result;
    }
    result.opened = true;

    std::string line;
    while (std::getline(in, line)) {
        auto parse = parseCaptureLine(line);
        switch (parse.result) {
            case CaptureParseResult::Frame:
                result.frames.push_back(parse.frame);
                break;
            case CaptureParseResult::NotAFrame:
                // Header / status / escaped noise / blank — ignore silently.
                break;
            case CaptureParseResult::Malformed:
                ++result.skippedLines;
                break;
        }
    }
    return result;
}

} // namespace vehicle_sim::domain
