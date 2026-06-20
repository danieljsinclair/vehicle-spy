#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vehicle_sim::domain {

/**
 * A single raw CAN frame parsed from a capture file.
 *
 * Capture files are produced by scripts/serial_to_csv.py. Two line formats are
 * supported (detected automatically, line by line):
 *   - verbatim notepad:  "timestamp_ms,raw_line"  where raw_line is the
 *     firmware's ELM327 text "<CANID> <D0> ... <D7>" (e.g. "118 3C 00 18").
 *   - legacy CSV:        "timestamp_ms,can_id,dlc,data_hex"
 */
struct RawFrame {
    std::uint64_t timestampMs = 0;
    std::uint32_t canId = 0;                 // CAN identifier (hex in the file)
    std::uint8_t dlc = 0;                    // data length code (0-8)
    std::array<std::uint8_t, 8> data{};      // payload, zero-padded to 8 bytes

    friend bool operator==(const RawFrame& a, const RawFrame& b) noexcept {
        return a.timestampMs == b.timestampMs
            && a.canId == b.canId
            && a.dlc == b.dlc
            && a.data == b.data;
    }
};

/** Outcome of parsing a single capture line. */
enum class CaptureParseResult {
    Frame,       // a valid CAN frame was parsed (see CaptureLineParse::frame)
    NotAFrame,   // not a frame and not malformed: header, status text, escaped
                 // noise, or a blank line — callers should ignore silently
    Malformed,   // looks like a frame line but is broken (bad hex, too many
                 // fields, non-numeric timestamp) — callers may count as skipped
};

/** Result of parsing one capture line. */
struct CaptureLineParse {
    CaptureParseResult result = CaptureParseResult::NotAFrame;
    RawFrame frame;                          // valid only when result == Frame
};

/** Outcome of loading a capture file. */
struct CaptureFile {
    std::vector<RawFrame> frames;
    std::size_t skippedLines = 0;            // lines that looked like frames but failed
    bool opened = false;                     // false if the file could not be opened
};

/**
 * Parse one capture line in either supported format (see RawFrame docs).
 * - Returns Frame with a populated RawFrame on success.
 * - Returns NotAFrame for header rows, blank lines, status text, and escaped
 *   noise (these are normal in a verbatim notepad capture and NOT errors).
 * - Returns Malformed for frame-shaped lines that fail to decode.
 * Tolerates a trailing carriage return (CRLF files).
 */
[[nodiscard]] CaptureLineParse parseCaptureLine(std::string_view line) noexcept;

/**
 * Convert a parsed frame to the 10-byte TWAI binary frame consumed by
 * DBCTranslationService::processFrame: [canId lo, canId hi, d0..d7].
 */
[[nodiscard]] std::vector<std::uint8_t> toTwaiFrame(const RawFrame& frame) noexcept;

/**
 * Load and parse a capture file. Header/blank lines are ignored; other
 * unparseable lines are counted in result.skippedLines (not fatal).
 */
[[nodiscard]] CaptureFile loadCaptureFile(const std::string& path) noexcept;

} // namespace vehicle_sim::domain
