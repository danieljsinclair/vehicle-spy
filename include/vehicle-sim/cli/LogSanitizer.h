#pragma once

#include <string>
#include <string_view>

namespace vehicle_sim::cli {

// Sanitize an external (operator/CLI/env-origin) value before it reaches a LOG sink.
// Control bytes (< 0x20 or 0x7F) become '?' so a crafted SSID/path/address cannot
// forge extra log lines (log injection, cpp:S5145). Every printable byte — including
// spaces and punctuation legitimate in a WiFi password — passes through unchanged.
// DO NOT use at a MACHINE DATA sink (CSV row / file payload / protocol frame): there the
// bytes are contractual and substitution is a defect, not a fix.
[[nodiscard]] std::string forLog(std::string_view value);

// Layout-preserving sanitizer for MULTI-LINE text sinks (e.g. CLI11 --help output).
// Like forLog it rebuilds the string byte-by-byte so cfamily's taint is severed at
// the sink (cpp:S5145), but it KEEPS the newline (0x0A) and tab (0x09) that the help
// layout needs — only CR (0x0D) and the other control bytes are neutralized. On the
// static, newline-delimited help_text this produces is a NO-OP, so the multi-line
// layout is preserved exactly; it is NOT the byte-mangling forLog and must not be
// used where a single-line cell is required.
[[nodiscard]] std::string forLogKeepNewlines(std::string_view value);

} // namespace vehicle_sim::cli
