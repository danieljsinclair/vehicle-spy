#include "StatusLEDRenderer.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace firmware {

// ── Pattern Registry ─────────────────────────────────────────────────────────────
// Single source of truth for pattern metadata (names, categories, descriptions).
// The visual rendering is computed from PATTERN_REGISTRY + the LEDStep opcode
// arrays (StatusLED.cpp); there is no hand-maintained help text. Adding a
// pattern to StatusLED.cpp + this registry is sufficient to have it appear
// in --led-help output by construction.
static const std::vector<PatternInfo> PATTERN_REGISTRY = {
    {StatusLED::Pattern::BOOT,                  PatternCategory::BOOT,          "BOOT",                     "Startup sequence"},
    {StatusLED::Pattern::WIFI_SEARCHING,        PatternCategory::WIFI,          "WIFI_SEARCHING",           "Searching for WiFi network"},
    {StatusLED::Pattern::WIFI_CONNECTED,        PatternCategory::WIFI,          "WIFI_CONNECTED",           "WiFi connected, no client"},
    {StatusLED::Pattern::CLIENT_CONNECTED,      PatternCategory::CONNECTION,    "CLIENT_CONNECTED",         "Client connected via BLE"},
    {StatusLED::Pattern::AP_MODE,               PatternCategory::AP_MODE,       "AP_MODE",                  "WiFi AP mode (no STA connection)"},
    {StatusLED::Pattern::OTA_IN_PROGRESS,       PatternCategory::OTA,           "OTA_IN_PROGRESS",          "Firmware update in progress"},
    {StatusLED::Pattern::ERROR_AUTH_FAILURE,    PatternCategory::ERROR,         "ERROR_AUTH_FAILURE",       "Authentication failed"},
    {StatusLED::Pattern::ERROR_RECOVERABLE,     PatternCategory::ERROR,         "ERROR_RECOVERABLE",        "Recoverable error occurred"},
    {StatusLED::Pattern::ERROR_NO_NTP_SERVICE,  PatternCategory::ERROR,         "ERROR_NO_NTP_SERVICE",     "NTP time service unavailable"},
    {StatusLED::Pattern::FATAL_UNRECOVERABLE,   PatternCategory::FATAL,         "FATAL_UNRECOVERABLE",      "Fatal error (system halted)"},
    {StatusLED::Pattern::OFF,                   PatternCategory::OFF,           "OFF",                      "LED off"}
};

// ── Total period (ms) for a pattern ─────────────────────────────────────────────
// Sum of all step durations. Used to size the visual layout and to place
// the final '|' divider. Single source of truth: derived from the LEDStep
// array, not hand-maintained.
static uint32_t totalPeriodMs(StatusLED::Pattern pattern) {
    auto [steps, count] = StatusLED::getPatternSteps(pattern);
    uint32_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        total += steps[i].durationMs;
    }
    return total;
}

// ── Render Pattern (divider-aligned visual) ─────────────────────────────────────
// One character per 100ms:
//   '-' = LED ON
//   ' ' = LED OFF
//   '#' = SOLID ON (single-state ON pattern)
//   '.' = SOLID OFF (single-state OFF pattern)
// A '|' divider is placed at every whole-second boundary — the start, the end,
// and each whole second in between. This makes long separators (e.g. 2s
// separator = '|          |          |') and 0.8s flashes ('|--------|')
// immediately readable.
//
// Examples:
//   BOOT (ON 500ms, OFF 500ms)            -> "|-----|     |"
//   WIFI_SEARCHING (ON 100, OFF 900)       -> "|-         |"
//   WIFI_CONNECTED (ON 800, OFF 200)       -> "|--------| |"
//   CLIENT_CONNECTED (ON 2000 solid)      -> "|#################|"
//   AP_MODE (1.4s pulses + 2s separator)  -> "|-------- - -         |"
//   OFF (OFF 2000 solid)                  -> "|...................|"
std::string StatusLEDRenderer::renderPattern(StatusLED::Pattern pattern) {
    auto [steps, stepCount] = StatusLED::getPatternSteps(pattern);
    if (stepCount == 0) return "";

    // Total period in tenths of a second (chars). For single-state patterns
    // (CLIENT_CONNECTED, OFF) the duration is a SEPARATOR_MS constant that
    // dictates the cycle length, so it is still informative.
    const uint32_t periodMs = totalPeriodMs(pattern);
    if (periodMs == 0) return "";

    // Special-case: single-state patterns (SOLID ON / SOLID OFF) are
    // unambiguous, so use a dedicated glyph instead of '-' / ' '.
    const bool isSingleState = (stepCount == 1);

    std::ostringstream out;
    out << '|';  // Start divider (t = 0s boundary)

    // Walk the pattern step-by-step, emitting one char per 100ms. Track
    // elapsed tenths so we can insert '|' dividers at every whole-second
    // boundary (1s, 2s, ...).
    size_t stepIdx = 0;
    uint32_t stepRemaining = steps[0].durationMs;
    LEDState stepState = steps[0].state;

    // 0.1s units elapsed so far (so we know when the next whole-second
    // boundary is reached).
    uint32_t tenthsElapsed = 0;
    const uint32_t totalTenths = periodMs / 100;

    while (tenthsElapsed < totalTenths) {
        // Place a '|' divider at the next whole-second boundary (before
        // emitting the char for that tenth) — i.e. at tenthsElapsed == 10, 20, ...
        if (tenthsElapsed > 0 && tenthsElapsed % 10 == 0) {
            out << '|';
        }

        // Pick the glyph for this 0.1s slot from the current step.
        char glyph;
        if (isSingleState) {
            glyph = (stepState == LEDState::ON) ? '#' : '.';
        } else if (stepState == LEDState::ON) {
            glyph = '-';
        } else {
            glyph = ' ';  // OFF or SEPARATOR (both render as gap)
        }
        out << glyph;
        ++tenthsElapsed;
        // Each iteration represents 100ms of pattern time, so consume 100ms
        // from the current step's remaining duration.
        stepRemaining = (stepRemaining > 100) ? stepRemaining - 100 : 0;
        if (stepRemaining == 0 && stepIdx + 1 < stepCount) {
            ++stepIdx;
            stepState = steps[stepIdx].state;
            stepRemaining = steps[stepIdx].durationMs;
        }
    }

    out << '|';  // End divider
    return out.str();
}

// ── Build the comment column for a pattern ──────────────────────────────────────
// Human-readable timing note. e.g. "ON 0.1s, OFF 0.9s" / "solid ON".
// Extracted from renderPattern() so the aligned table column is the same
// string that the key/footer describes.
std::string StatusLEDRenderer::timingNote(StatusLED::Pattern pattern) {
    auto [steps, stepCount] = StatusLED::getPatternSteps(pattern);
    if (stepCount == 0) return "";

    if (stepCount == 1) {
        return steps[0].state == LEDState::ON ? "solid ON" : "solid OFF";
    }

    std::ostringstream note;
    for (size_t i = 0; i < stepCount; ++i) {
        const LEDStep& step = steps[i];
        if (i > 0) note << ", ";
        // Patterns encode the SEPARATOR as {OFF, SEPARATOR_MS} (a long OFF),
        // not as LEDState::SEPARATOR. Detect the long-pause case from the
        // duration so the timing note tells the user "this is a separator".
        const bool isSeparator =
            (step.state == LEDState::SEPARATOR) ||
            (step.state == LEDState::OFF && step.durationMs >= StatusLEDConstants::SEPARATOR_MS);
        if (isSeparator) {
            note << "sep " << formatDuration(step.durationMs);
        } else if (step.state == LEDState::ON) {
            note << "ON " << formatDuration(step.durationMs);
        } else {
            note << "OFF " << formatDuration(step.durationMs);
        }
    }
    return note.str();
}

// ── Build the C++ enum-name line for a pattern ──────────────────────────────────
// Mirrors the StatusLED::Pattern enum spelling so users can grep from CLI
// output to source. E.g. "StatusLED::Pattern::WIFI_SEARCHING".
std::string StatusLEDRenderer::enumName(StatusLED::Pattern pattern) {
    const char* bare = nullptr;
    switch (pattern) {
        case StatusLED::Pattern::BOOT:                 bare = "BOOT"; break;
        case StatusLED::Pattern::WIFI_SEARCHING:       bare = "WIFI_SEARCHING"; break;
        case StatusLED::Pattern::WIFI_CONNECTED:       bare = "WIFI_CONNECTED"; break;
        case StatusLED::Pattern::CLIENT_CONNECTED:     bare = "CLIENT_CONNECTED"; break;
        case StatusLED::Pattern::AP_MODE:              bare = "AP_MODE"; break;
        case StatusLED::Pattern::OTA_IN_PROGRESS:      bare = "OTA_IN_PROGRESS"; break;
        case StatusLED::Pattern::ERROR_AUTH_FAILURE:   bare = "ERROR_AUTH_FAILURE"; break;
        case StatusLED::Pattern::ERROR_RECOVERABLE:    bare = "ERROR_RECOVERABLE"; break;
        case StatusLED::Pattern::ERROR_NO_NTP_SERVICE: bare = "ERROR_NO_NTP_SERVICE"; break;
        case StatusLED::Pattern::FATAL_UNRECOVERABLE:  bare = "FATAL_UNRECOVERABLE"; break;
        case StatusLED::Pattern::OFF:                  bare = "OFF"; break;
    }
    return std::string{"StatusLED::Pattern::"} + (bare ? bare : "?");
}

// ── Compute the max visual width across all patterns ────────────────────────────
// Used to right-align the comment column so every row's # note lines up
// (visual column width is data-driven — derived from the registered patterns,
// not a hard-coded constant).
static size_t maxVisualWidth() {
    size_t maxWidth = 0;
    for (const auto& info : PATTERN_REGISTRY) {
        maxWidth = std::max(maxWidth, StatusLEDRenderer::renderPattern(info.pattern).size());
    }
    return maxWidth;
}

// ── Generate --led-help table ────────────────────────────────────────────────────
// Format (one row per pattern):
//   NAME  <visual>  # comment
//   e.g. "WIFI_CONNECTED  |--------| |  # ON 0.8s, OFF 0.2s"
// Header explains the encoding; the key at the bottom names the dividers and
// the 100ms-per-char rule. Fully data-driven: visuals + comments + enum names
// all derived from the LEDStep arrays via StatusLED::getPatternSteps() and the
// PATTERN_REGISTRY above.
std::string StatusLEDRenderer::generateTable() {
    std::ostringstream out;

    // ── Header
    out << "Status LED patterns (1 char = 100ms, |'s mark each whole second)\n";

    // ── Compute the visual column width for alignment
    const size_t visualWidth = maxVisualWidth();

    // ── Name column width: longest name in the registry
    size_t nameWidth = 0;
    for (const auto& info : PATTERN_REGISTRY) {
        nameWidth = std::max(nameWidth, std::strlen(info.name));
    }

    // ── Per-pattern rows
    for (const auto& info : PATTERN_REGISTRY) {
        std::string visual = renderPattern(info.pattern);
        std::string note = timingNote(info.pattern);
        out << "  " << std::left << std::setw(static_cast<int>(nameWidth)) << info.name
            << "  " << std::left << std::setw(static_cast<int>(visualWidth)) << visual
            << "  # " << note << "\n";
    }

    out << "\nEnum names (use with setPattern / getPattern):\n";
    for (const auto& info : PATTERN_REGISTRY) {
        out << "  " << enumName(info.pattern) << "\n";
    }

    out << "\nKey:\n"
        << "  1 char = 100ms     '|' divider = every whole second (start, 1s, 2s, ...)\n"
        << "  '-' = LED ON       ' ' = LED OFF       '#' = solid ON       '.' = solid OFF\n";

    return out.str();
}

// ── Generate Help Text (long form) ──────────────────────────────────────────────
// Same data-driven approach as generateTable() but with per-pattern detail
// blocks (description, full timing breakdown). Kept for --help / external
// consumers that want the verbose form.
std::string StatusLEDRenderer::generateHelpText() {
    std::ostringstream help;

    help << "Status LED Patterns\n";
    help << "===================\n\n";
    help << "Visual key:\n";
    help << "  '-' = LED ON\n";
    help << "  ' ' = LED OFF\n";
    help << "  '#' = SOLID ON\n";
    help << "  '.' = SOLID OFF\n";
    help << "  '|' = whole-second boundary (start, 1s, 2s, ...)\n\n";
    help << "Each character represents 100ms. Patterns repeat continuously.\n\n";

    // Group patterns by category
    std::map<PatternCategory, std::vector<const PatternInfo*>> grouped;
    for (const auto& patternInfo : PATTERN_REGISTRY) {
        grouped[patternInfo.category].push_back(&patternInfo);
    }

    // Render each category
    for (const auto& [category, patterns] : grouped) {
        help << "## " << getCategoryName(category) << "\n\n";

        for (const auto* patternInfo : patterns) {
            help << patternInfo->name << "\n";
            help << "  " << patternInfo->description << "\n";

            // Render visual representation
            std::string visual = renderPattern(patternInfo->pattern);
            help << "  Visual: " << visual << "\n";

            // Generate human-readable timing note
            auto [steps, stepCount] = StatusLED::getPatternSteps(patternInfo->pattern);
            if (stepCount > 0) {
                help << "  Timing: ";
                for (size_t i = 0; i < stepCount; ++i) {
                    const LEDStep& step = steps[i];

                    if (i > 0) {
                        help << ", ";
                    }

                    if (step.state == LEDState::SEPARATOR) {
                        help << "separator (" << formatDuration(step.durationMs) << ")";
                    } else if (step.state == LEDState::ON) {
                        help << "ON for " << formatDuration(step.durationMs);
                    } else if (step.state == LEDState::OFF) {
                        help << "OFF for " << formatDuration(step.durationMs);
                    }
                }
                help << "\n";
            }

            help << "  Enum:   " << enumName(patternInfo->pattern) << "\n\n";
        }

        help << "\n";
    }

    return help.str();
}

// ── Get Category Name ───────────────────────────────────────────────────────────────
const char* StatusLEDRenderer::getCategoryName(PatternCategory category) {
    switch (category) {
        case PatternCategory::BOOT:         return "Boot & Initialization";
        case PatternCategory::WIFI:         return "WiFi Connection States";
        case PatternCategory::CONNECTION:   return "Client Connection";
        case PatternCategory::AP_MODE:      return "Access Point Mode";
        case PatternCategory::OTA:          return "Firmware Updates";
        case PatternCategory::ERROR:        return "Error States (Recoverable)";
        case PatternCategory::FATAL:        return "Fatal Errors (Unrecoverable)";
        case PatternCategory::OFF:          return "Power Off";
    }
    return "Unknown";
}

// ── Format Duration ───────────────────────────────────────────────────────────────────
std::string StatusLEDRenderer::formatDuration(uint32_t durationMs) {
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1);
    formatted << (durationMs / 1000.0) << "s";
    return formatted.str();
}

} // namespace firmware
