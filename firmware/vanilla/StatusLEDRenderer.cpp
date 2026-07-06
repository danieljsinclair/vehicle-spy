#include "StatusLEDRenderer.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace firmware {

// ── Pattern Registry ─────────────────────────────────────────────────────────────
// Single source of truth for pattern metadata (names, categories, descriptions)
static const std::vector<PatternInfo> PATTERN_REGISTRY = {
    {StatusLED::Pattern::BOOT,                  PatternCategory::BOOT,          "BOOT",                     "Startup sequence"},
    {StatusLED::Pattern::WIFI_SEARCHING,        PatternCategory::WIFI,          "WIFI_SEARCHING",           "Searching for WiFi network"},
    {StatusLED::Pattern::WIFI_CONNECTED,        PatternCategory::WIFI,          "WIFI_CONNECTED",           "WiFi connected, no client"},
    {StatusLED::Pattern::CLIENT_CONNECTED,      PatternCategory::CONNECTION,    "CLIENT_CONNECTED",         "Client connected via BLE"},
    {StatusLED::Pattern::AP_MODE,               PatternCategory::AP_MODE,       "AP_MODE",                  "WiFi AP mode (no STA connection)"},
    {StatusLED::Pattern::OTA_IN_PROGRESS,       PatternCategory::OTA,           "OTA_IN_PROGRESS",          "Firmware update in progress"},
    {StatusLED::Pattern::AUTH_FAILURE,          PatternCategory::ERROR,         "AUTH_FAILURE",             "Authentication failed"},
    {StatusLED::Pattern::ERROR_RECOVERABLE,     PatternCategory::ERROR,         "ERROR_RECOVERABLE",        "Recoverable error occurred"},
    {StatusLED::Pattern::ERROR_NO_NTP_SERVICE,  PatternCategory::ERROR,         "ERROR_NO_NTP_SERVICE",     "NTP time service unavailable"},
    {StatusLED::Pattern::FATAL_UNRECOVERABLE,   PatternCategory::FATAL,         "FATAL_UNRECOVERABLE",      "Fatal error (system halted)"},
    {StatusLED::Pattern::OFF,                   PatternCategory::OFF,           "OFF",                      "LED off"}
};

// ── Render Pattern ───────────────────────────────────────────────────────────────
std::string StatusLEDRenderer::renderPattern(StatusLED::Pattern pattern) {
    auto [steps, stepCount] = StatusLED::getPatternSteps(pattern);
    if (stepCount == 0) return "";

    std::ostringstream rendered;
    bool isSingleStatePattern = (stepCount == 1);
    bool isSolidOnPattern = isSingleStatePattern && (steps[0].state == LEDState::ON);
    bool isSolidOffPattern = isSingleStatePattern && (steps[0].state == LEDState::OFF);

    // Handle solid patterns with special characters
    if (isSolidOnPattern) {
        // Solid ON pattern (CLIENT_CONNECTED)
        // Use '#' for SOLID, cap at 10 chars for visual clarity
        return "##########";  // Fixed representation for solid ON
    }

    if (isSolidOffPattern) {
        // Solid OFF pattern (OFF)
        // Use spaces for solid OFF, cap at 10 chars for visual clarity
        return "          ";  // Fixed representation for solid OFF
    }

    // Multi-step pattern - render each step
    for (size_t i = 0; i < stepCount; ++i) {
        const LEDStep& step = steps[i];
        int chars = static_cast<int>(std::round(step.durationMs / 100.0));

        // Detect separator: long OFF duration (SEPARATOR_MS or very long gap)
        bool isSeparator = (step.state == LEDState::SEPARATOR) ||
                          (step.state == LEDState::OFF && step.durationMs >= StatusLEDConstants::SEPARATOR_MS);

        if (isSeparator) {
            // Render SEPARATOR as '|' (capped representation)
            rendered << '|';
        } else if (step.state == LEDState::ON) {
            // Render ON as '-', cap long durations for readability
            int visualChars = std::min(chars, 10);  // Cap at 10 chars
            rendered << std::string(visualChars, '-');
        } else if (step.state == LEDState::OFF) {
            // Render OFF as ' ', cap long durations for readability
            int visualChars = std::min(chars, 10);  // Cap at 10 chars
            rendered << std::string(visualChars, ' ');
        }
    }

    return rendered.str();
}

// ── Generate Diagnostic Table (compact, one line per pattern) ─────────────────────
// Format: "NAME  <visual>  # <timing note>" — one pattern per line, grouped by
// category. 1 char = 100ms: '-' ON, ' ' OFF, '#' SOLID ON, '|' SEPARATOR.
std::string StatusLEDRenderer::generateTable() {
    std::ostringstream out;
    out << "Status LED diagnostic — 1 char = 100ms ('-' on, ' ' off, '#' solid, '|' sep)\n\n";

    // Group by category, preserving registry order within each group.
    std::map<PatternCategory, std::vector<const PatternInfo*>> grouped;
    for (const auto& info : PATTERN_REGISTRY) {
        grouped[info.category].push_back(&info);
    }

    for (const auto& [category, patterns] : grouped) {
        out << "# " << getCategoryName(category) << "\n";
        for (const auto* info : patterns) {
            std::string visual = renderPattern(info->pattern);
            std::string note = timingNote(info->pattern);
            out << "  " << std::left << std::setw(22) << info->name
                << visual << "  # " << note << "\n";
        }
        out << "\n";
    }
    return out.str();
}

// Brief inline timing note (e.g. "ON 0.1s, OFF 0.9s" or "solid ON").
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
        if (step.state == LEDState::SEPARATOR) {
            note << "sep " << formatDuration(step.durationMs);
        } else if (step.state == LEDState::ON) {
            note << "ON " << formatDuration(step.durationMs);
        } else {
            note << "OFF " << formatDuration(step.durationMs);
        }
    }
    return note.str();
}

// ── Generate Help Text ─────────────────────────────────────────────────────────────
std::string StatusLEDRenderer::generateHelpText() {
    std::ostringstream help;

    help << "Status LED Patterns\n";
    help << "===================\n\n";
    help << "Visual key:\n";
    help << "  '-' = LED ON\n";
    help << "  ' ' = LED OFF\n";
    help << "  '#' = SOLID ON\n";
    help << "  '|' = SEPARATOR (long pause before repeat)\n\n";
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

            help << "\n";
        }

        help << "\n";
    }

    return help.str();
}

// ── Get Pattern Info ───────────────────────────────────────────────────────────────
PatternInfo StatusLEDRenderer::getPatternInfo(StatusLED::Pattern pattern) {
    for (const auto& patternInfo : PATTERN_REGISTRY) {
        if (patternInfo.pattern == pattern) {
            return patternInfo;
        }
    }
    return {StatusLED::Pattern::OFF, PatternCategory::OFF, "OFF", "LED off"};
}

// ── Get Category Name ───────────────────────────────────────────────────────────────
const char* StatusLEDRenderer::getCategoryName(PatternCategory category) {
    switch (category) {
        case PatternCategory::BOOT: return "Boot & Initialization";
        case PatternCategory::WIFI: return "WiFi Connection States";
        case PatternCategory::CONNECTION: return "Client Connection";
        case PatternCategory::AP_MODE: return "Access Point Mode";
        case PatternCategory::OTA: return "Firmware Updates";
        case PatternCategory::ERROR: return "Error States (Recoverable)";
        case PatternCategory::FATAL: return "Fatal Errors (Unrecoverable)";
        case PatternCategory::OFF: return "Power Off";
        default: return "Other";
    }
}

// ── Format Duration ───────────────────────────────────────────────────────────────────
std::string StatusLEDRenderer::formatDuration(uint32_t durationMs) {
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1);
    formatted << (durationMs / 1000.0) << "s";
    return formatted.str();
}

// ── Get All Patterns ────────────────────────────────────────────────────────────────
const std::vector<PatternInfo> StatusLEDRenderer::getAllPatterns() {
    return PATTERN_REGISTRY;
}

} // namespace firmware
