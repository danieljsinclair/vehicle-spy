#pragma once

#include <string>
#include <string_view>

namespace vehicle_sim::telemetry {

/**
 * Render one CSV cell from an already-formatted numeric string, keeping only
 * the bytes a numeric cell is allowed to contain.
 *
 * WHY THIS EXISTS (correctness first, cpp:S5145 second)
 *
 * Every numeric column in the decoded-telemetry schema (timestamp_ms, the
 * optional<double> signals, dbc_signal_count) is derived from EXTERNAL data:
 * a replayed capture file or a live CAN/serial stream. A CSV cell must never
 * contain a delimiter or a record terminator — a comma would shift every later
 * column, and a CR/LF would split one record into two. Numeric formatting is
 * expected to be safe, but "expected" is not "guaranteed" across locales
 * (a locale-imbued stream renders a decimal comma) and non-finite values
 * (inf/nan). This is the structural guarantee that a numeric cell stays one
 * cell.
 *
 * The whitelist is deliberately tight: digits, '-', '+', '.', and 'e'/'E' for
 * exponents. That is the complete alphabet of the numbers this schema emits, so
 * on every value the pipeline actually produces this function is a NO-OP and the
 * byte-identical CSV contract is preserved exactly. Anything outside the
 * alphabet is dropped rather than substituted, because a numeric column has no
 * meaningful placeholder byte (unlike a text column, where forLog() uses '?').
 *
 * It also severs cfamily's taint at the sink: the value written to the stream is
 * built here from a validated byte alphabet rather than streamed straight from
 * the external number, which is the same sanitizer shape as cli::forLog() for
 * text. No suppression required.
 */
[[nodiscard]] inline std::string csvNumericCell(std::string_view formatted) {
    std::string cell;
    cell.reserve(formatted.size());
    for (const char ch : formatted) {
        const bool isDigit    = ch >= '0' && ch <= '9';
        const bool isSign     = ch == '-' || ch == '+';
        const bool isPoint    = ch == '.';
        const bool isExponent = ch == 'e' || ch == 'E';
        if (isDigit || isSign || isPoint || isExponent) {
            cell += ch;
        }
    }
    return cell;
}

} // namespace vehicle_sim::telemetry
