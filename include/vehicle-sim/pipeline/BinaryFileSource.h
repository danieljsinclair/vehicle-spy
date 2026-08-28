#pragma once

#include "vehicle-sim/pipeline/IFrameSource.h"

#include <fstream>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Reads a raw.txt capture file and emits one TwaiFrame per record. The
 * capture format is mixed:
 *
 *   - ASCII rows:   "<timestamp_ms>,<CANID> <D0> <D1> ... <D7>"
 *                   e.g. "1785964637479,3 0B 24"
 *   - Binary rows:  "<timestamp_ms>,<10 raw TWAI bytes>"
 *                   where the timestamp is the decimal text BEFORE the comma
 *                   and the 10 bytes AFTER the comma are the raw TWAI frame
 *                   (canId_lo, canId_hi, d0..d7).
 *
 * The ASCII/binary choice is sniffed per line: if the byte immediately after
 * the comma is a hex digit followed by whitespace/comma/EOF we go ASCII and
 * tokenise the line; otherwise we treat the 10 bytes after the comma as a
 * raw TWAI frame. Lines that fail both decodes are skipped silently.
 */
class BinaryFileSource final : public IFrameSource {
public:
    explicit BinaryFileSource(std::string filePath) : filePath_(std::move(filePath)) {}

    bool open() noexcept override;
    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] std::optional<TwaiFrame> nextFrame() noexcept override;

private:
    // Read the next record's "timestamp,payload" pair. Returns false at EOF.
    bool readNext(std::string& tsOut, std::string& payloadOut);
    // Parse an ASCII payload (already-split token string) into a TwaiFrame.
    // payload == the part after the comma; may contain trailing \r.
    std::optional<TwaiFrame> parseAscii(std::uint64_t tsMs, std::string_view payload) const;
    // Parse a binary payload (10 raw bytes) into a TwaiFrame.
    std::optional<TwaiFrame> parseBinary(std::uint64_t tsMs, std::string_view payload) const;

    std::string filePath_;
    std::ifstream stream_;
};

} // namespace vehicle_sim::pipeline
