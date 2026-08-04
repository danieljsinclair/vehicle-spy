#pragma once

// CsvShape.h — shared, behaviour-level assertions helpers for the decoded-CSV
// schema.
//
// Why this exists: the CSV contract that matters to a caller is its SHAPE (one
// record per line, 13 named columns) and the VALUES in named columns — not the
// literal text of a whole line. Tests that restate a full row are fragile: a
// column added at the end, or two columns swapped, breaks every row test at
// once and says nothing about which behaviour regressed.
//
// These helpers let a test name the column it cares about. The column names
// come from the header the writer itself emitted, so no test restates the
// schema — CsvRowFormatter stays its single source of truth (DRY), and the
// byte-identity tests against the real TraceLogger remain the pin for the
// exact bytes.

#include <algorithm>
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace vehicle_sim::test {

/// Columns in the decoded-CSV schema: a well-formed record has this many
/// fields, i.e. CSV_FIELD_COUNT - 1 separating commas.
inline constexpr std::size_t CSV_FIELD_COUNT = 13;

/// Split text into lines (no trailing empty line for a newline-terminated
/// stream), so a test can reason per record.
inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

/// Split one CSV record on commas, PRESERVING empty cells (including a
/// trailing one) — "absent" cells are meaningful in this schema, so a splitter
/// that drops them would hide the very thing some tests assert.
inline std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    std::size_t comma = line.find(',');
    while (comma != std::string::npos) {
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
        comma = line.find(',', start);
    }
    fields.push_back(line.substr(start));
    return fields;
}

/// Zip an emitted header line with an emitted row into column-name -> cell, so
/// assertions read `cells.at("speed_kmh")` instead of counting positions.
/// Reordering columns keeps these tests passing; renaming one fails them —
/// which is the correct sensitivity.
inline std::map<std::string, std::string> cellsByColumn(
    const std::string& headerLine,
    const std::string& rowLine
) {
    const auto names = splitFields(headerLine);
    const auto values = splitFields(rowLine);
    const auto width = std::min(names.size(), values.size());

    std::map<std::string, std::string> cells;
    for (std::size_t i = 0; i < width; ++i) {
        cells.emplace(names[i], values[i]);
    }
    return cells;
}

/// True when text begins with prefix. Used to assert a piped stream opens with
/// the schema line, which is what tells a downstream parser it is CSV.
inline bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

} // namespace vehicle_sim::test
