#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace vehicle_sim::cli {

// Parse a --start-from timecode into seconds.
//
// Grammar (mirrors engine-sim-cli's parseReplayTimeToSeconds — the two CLIs
// must accept the same timecodes):
//   "<seconds>"       e.g. "90", "90.5"
//   "<mm>:<ss>"       e.g. "01:30"
//   "<hh>:<mm>:<ss>"  e.g. "1:00:00"
// Returns -1.0 when the input is not a valid timecode (empty, stray colons,
// non-numeric fields, more than three fields, or a field out of double
// range). -1.0 is the "unset / invalid" sentinel callers check for.
inline double parseTimecodeToSeconds(const std::string& s) {
    if (s.empty()) return -1.0;

    // Reject trailing/leading colons (std::getline silently drops empty tokens
    // at the ends, so "01:" would parse as ["01"] — treat as invalid).
    if (s.front() == ':' || s.back() == ':') return -1.0;

    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ':')) {
        parts.push_back(part);
    }

    try {
        if (parts.size() == 1) {
            return std::stod(parts[0]);
        }
        if (parts.size() == 2) {
            return std::stod(parts[0]) * 60.0 + std::stod(parts[1]);
        }
        if (parts.size() == 3) {
            return std::stod(parts[0]) * 3600.0
                 + std::stod(parts[1]) * 60.0
                 + std::stod(parts[2]);
        }
    } catch (const std::invalid_argument&) {
        // std::stod: token isn't a number.
        return -1.0;
    } catch (const std::out_of_range&) {
        // std::stod: token parses but the value is out of double range.
        return -1.0;
    }

    return -1.0;
}

} // namespace vehicle_sim::cli
