#include "vehicle-sim/pipeline/LiveTwaiSource.h"
#include "vehicle-sim/pipeline/ITransport.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vehicle_sim::pipeline {

namespace {

// Advance `i` past spaces/tabs and return the next whitespace-delimited
// token (empty when the remainder is all separators). The single mutation
// point for the scan index, so the tokenise loop stays flat (cpp:S3776).
std::string_view nextSpaceToken(std::string_view s, std::size_t& i) noexcept {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    const std::size_t begin = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
    return s.substr(begin, i - begin);
}

// First token = the 11-bit CAN id: at most 3 hex digits and in range.
bool parseCanIdToken(std::string_view tok, std::uint32_t v,
                     std::uint32_t& canIdOut) noexcept {
    if (tok.size() > 3) return false;  // 11-bit CAN id is <= 3 hex digits
    if (v > 0x7FFu) return false;      // 11-bit range
    canIdOut = v;
    return true;
}

// A data-byte token: exactly 2 hex digits, <= 0xFF, at most 8 bytes.
bool appendDataByteToken(std::string_view tok, std::uint32_t v,
                         std::array<std::uint8_t, 8>& dataOut,
                         std::size_t& dlcOut) noexcept {
    if (tok.size() != 2) return false;  // data bytes are exactly 2 hex chars
    if (v > 0xFFu) return false;
    if (dlcOut >= 8) return false;
    dataOut[dlcOut++] = static_cast<std::uint8_t>(v);
    return true;
}

// Tokenise on whitespace; first token = CAN-ID (up to 3 hex digits, 11-bit),
// rest = data bytes (exactly 2 hex digits, 0-255).
bool tokenizeTwaiLine(std::string_view line, std::uint32_t& canIdOut,
                      std::array<std::uint8_t, 8>& dataOut,
                      std::size_t& dlcOut) {
    dlcOut = 0;
    canIdOut = 0;
    bool haveId = false;
    std::size_t i = 0;
    for (std::string_view tok = nextSpaceToken(line, i); !tok.empty();
         tok = nextSpaceToken(line, i)) {
        std::uint32_t v = 0;
        if (auto r = std::from_chars(tok.data(), tok.data() + tok.size(), v, 16);
            r.ec != std::errc{}) return false;
        if (!haveId) {
            if (!parseCanIdToken(tok, v, canIdOut)) return false;
            haveId = true;
        } else if (!appendDataByteToken(tok, v, dataOut, dlcOut)) {
            return false;
        }
    }
    return haveId;
}

std::uint64_t wallclockMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string_view rtrim(std::string_view s) noexcept {
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\r'
               || s.back() == '\n' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

LiveTwaiSource::LiveTwaiSource(ITransport& transport)
    : transport_(transport), normaliser_(nullptr) {}

LiveTwaiSource::LiveTwaiSource(ITransport& transport, IAdapterNormaliser* normaliser)
    : transport_(transport), normaliser_(normaliser) {}

bool LiveTwaiSource::open() noexcept { return transport_.open(); }

bool LiveTwaiSource::isOpen() const noexcept { return transport_.isOpen(); }

std::optional<TwaiFrame> LiveTwaiSource::nextFrame() noexcept {
    while (auto line = transport_.nextLine()) {
        const auto trimmed = rtrim(*line);
        if (trimmed.empty()) continue;

        if (normaliser_) {
            // ELM327 / future-normaliser path: defer parsing to the injected
            // normaliser (Skip / Malformed are silently ignored). The
            // normaliser sets its own timestampMs; we replace it with
            // wall-clock so the live path matches the rest of the pipeline.
            auto r = normaliser_->normalise(std::string(trimmed));
            if (r.kind != NormaliserResultKind::Frame) continue;
            TwaiFrame f = std::move(r.frame);
            f.timestampMs = wallclockMs();
            return f;
        }

        // Raw path: inline tokeniser on "<CANID> <D0> ... <D7>".
        std::uint32_t canId = 0;
        std::array<std::uint8_t, 8> data{};
        std::size_t dlc = 0;
        if (!tokenizeTwaiLine(trimmed, canId, data, dlc)) continue;

        TwaiFrame f;
        f.timestampMs = wallclockMs();
        f.bytes[0] = static_cast<std::uint8_t>(canId & 0xFF);
        f.bytes[1] = static_cast<std::uint8_t>((canId >> 8) & 0xFF);
        for (std::size_t k = 0; k < dlc; ++k) f.bytes[2 + k] = data[k];
        return f;
    }
    return std::nullopt;
}

} // namespace vehicle_sim::pipeline
