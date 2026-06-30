#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace vehicle_sim::pipeline {

/**
 * Writes the raw transport stream to "<base>.raw.txt". This is the SOURCE OF
 * TRUTH sink — every line the transport produced, stamped with the wall-clock
 * UTC millisecond timestamp at the moment of capture, so a capture can be
 * replayed later. It must not know about decode (the decoded CSV is derived
 * from this raw stream).
 *
 * Each output line has the form "<utc_ms>,<transport_line>" where <utc_ms> is
 * the epoch millisecond count at the time writeLine() is called and
 * <transport_line> is the verbatim transport text (e.g. "118 3C 00 18").
 * For Phase 1 file replay this sink is intentionally NOT instantiated: the
 * input file already is the source of truth, so writing a second copy would be
 * redundant. Phase 2 transports (BLE/TCP/USB) use it to persist a capture of
 * their live stream.
 */
class RawLogSink final {
public:
    /**
     * Open <base>.raw.txt for writing. isValid() will be false if the file
     * could not be created (no throw — differs from DecodedCsvSink because
     * the raw sink is optional in the pipeline).
     */
    explicit RawLogSink(const std::string& base);

    RawLogSink(const RawLogSink&) = delete;
    RawLogSink& operator=(const RawLogSink&) = delete;
    RawLogSink(RawLogSink&&) noexcept;
    RawLogSink& operator=(RawLogSink&&) noexcept;

    /**
     * Write one raw transport line prefixed with a wall-clock UTC millisecond
     * timestamp, appending a newline. Output format: "<utc_ms>,<line>\n".
     * No-op if invalid.
     */
    void writeLine(const std::string& line) noexcept;
    [[nodiscard]] bool isValid() const noexcept;

private:
    std::ofstream file_;
};

} // namespace vehicle_sim::pipeline
