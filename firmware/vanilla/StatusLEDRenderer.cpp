#include "StatusLEDRenderer.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace firmware {

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
    {StatusLED::Pattern::FATAL_UNRECOVERABLE_SOS, PatternCategory::FATAL,       "FATAL_UNRECOVERABLE_SOS",  "Fatal error (system halted, SOS)"},
    {StatusLED::Pattern::OFF,                   PatternCategory::OFF,           "OFF",                      "LED off"}
};

struct NamedConstant {
    const char* name;
    LEDState state;
    uint32_t durationMs;
};

static const std::vector<NamedConstant> NAMED_CONSTANTS = {
    {"TINY_FLASH",         LEDState::ON,  StatusLEDConstants::TINY_FLASH_MS},
    {"SHORT_FLASH",        LEDState::ON,  StatusLEDConstants::SHORT_FLASH_MS},
    {"MED_FLASH",          LEDState::ON,  StatusLEDConstants::MED_FLASH_MS},
    {"LONG_FLASH",         LEDState::ON,  StatusLEDConstants::LONG_FLASH_MS},
    {"VERY_LONG_FLASH",    LEDState::ON,  StatusLEDConstants::VERY_LONG_FLASH_MS},
    {"TINY_GAP",           LEDState::OFF, StatusLEDConstants::TINY_GAP_MS},
    {"SHORT_GAP",          LEDState::OFF, StatusLEDConstants::SHORT_GAP_MS},
    {"MED_GAP",            LEDState::OFF, StatusLEDConstants::MED_GAP_MS},
    {"LONG_GAP",           LEDState::OFF, StatusLEDConstants::LONG_GAP_MS},
    {"SEARCHING_GAP",      LEDState::OFF, StatusLEDConstants::SEARCHING_GAP_MS},
    {"SEPARATOR",          LEDState::OFF, StatusLEDConstants::SEPARATOR_MS}
};

#ifdef INCLUDE_LED_HELP_KEY
static std::string renderNamedConstant(const NamedConstant& c) {
    const uint32_t tenths = c.durationMs / 100;
    if (tenths == 0) return "|";
    std::ostringstream out;
    out << '|';
    for (uint32_t i = 0; i < tenths; ++i) {
        out << (c.state == LEDState::ON ? '-' : ' ');
    }
    out << '|';
    return out.str();
}
#endif

#ifdef INCLUDE_LED_HELP_KEY
static std::string renderNamedConstantPadded(const NamedConstant& c, size_t width) {
    std::string bare = renderNamedConstant(c);
    if (bare.size() >= width) return bare;
    return bare + std::string(width - bare.size(), ' ');
}
#endif

std::string StatusLEDRenderer::formatDuration(uint32_t durationMs) {
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1);
    formatted << (durationMs / 1000.0) << "s";
    return formatted.str();
}

static bool isTrailingSeparator(const LEDStep& step, bool isLast) {
    if (!isLast) return false;
    if (step.state == LEDState::SEPARATOR) return true;
    return step.state == LEDState::OFF
        && step.durationMs >= StatusLEDConstants::SEPARATOR_MS;
}

// ── timingNote() helpers ───────────────────────────────────────────────────
// timingNote() renders a pattern's step sequence as a compact human-readable
// string (e.g. "PULSE 0.2s", "3xPULSE 0.1s, ON 0.8s, OFF 0.2s"). The helpers
// below split that transformation into four single-responsibility stages so the
// public method reads top-to-bottom as prose.

// Stage 1 — collect the renderable steps, dropping the trailing separator (the
// existing isTrailingSeparator rule). This is the single source of truth for
// what is "shown" — both the visualizer and the timing note agree on what
// counts. Returns an empty vector when nothing survives the filter.
struct TimingStep { LEDState state; uint32_t ms; };

static std::vector<TimingStep> filterSteps(const LEDStep* steps, size_t stepCount) {
    std::vector<TimingStep> kept;
    for (size_t i = 0; i < stepCount; ++i) {
        if (isTrailingSeparator(steps[i], i + 1 == stepCount)) continue;
        kept.push_back({steps[i].state, steps[i].durationMs});
    }
    return kept;
}

// Stage 2 — tokenize into PULSE / DASH / ON / OFF tokens. Consecutive ON+OFF
// pairs (in either order) collapse:
//   * symmetric (equal duration)  → "PULSE Xs"
//   * asymmetric (one step >= 3x the other) → "DASH Xs" (X = the longer step)
// Near-symmetric asymmetric pairs are left as individual ON/OFF tokens — the
// simplest representation (no nested grouping). The 3x threshold keeps DASH
// firmly in the "clearly asymmetric" regime and never fires on pairs that are
// merely uneven. This is a PRESENTATION-only transformation; the underlying
// LEDStep{state, durationMs} model is unchanged.
enum TokenKind { TK_PULSE, TK_DASH, TK_ON, TK_OFF };
struct Token { TokenKind kind; uint32_t ms; };

static bool isDashPair(uint32_t a, uint32_t b) {
    if (a == 0 || b == 0) return false;
    const uint32_t lo = std::min(a, b);
    const uint32_t hi = std::max(a, b);
    return hi >= 3 * lo;  // one step at least 3x the other
}

static std::vector<Token> tokenizePulses(const std::vector<TimingStep>& steps) {
    std::vector<Token> tokens;
    for (size_t i = 0; i < steps.size(); ++i) {
        const bool twoConsecutive = (i + 1 < steps.size())
            && steps[i].state != steps[i + 1].state;  // one ON, one OFF
        if (twoConsecutive && steps[i].ms == steps[i + 1].ms) {
            // Symmetric pair → PULSE.
            tokens.push_back({TK_PULSE, steps[i].ms});
            ++i;  // consume the matching step
        } else if (twoConsecutive && isDashPair(steps[i].ms, steps[i + 1].ms)) {
            // Asymmetric long/short pair (either order) → DASH of the longer.
            tokens.push_back({TK_DASH, std::max(steps[i].ms, steps[i + 1].ms)});
            ++i;  // consume the matching step
        } else {
            tokens.push_back(steps[i].state == LEDState::ON
                                 ? Token{TK_ON, steps[i].ms}
                                 : Token{TK_OFF, steps[i].ms});
        }
    }
    return tokens;
}

// Stage 3 — collapse runs of N consecutive identical PULSE (or DASH) tokens into
// a single "NxPULSE Xs" / "NxDASH Xs" run. ON/OFF tokens are emitted
// individually (no run-collapse) to keep the representation simple and
// unambiguous. Returns a list of (count, token) pairs where count is the run
// length (1 for non-pulse/non-dash tokens).
struct Run { uint32_t count; Token token; };

static std::vector<Run> collapsePulseRuns(const std::vector<Token>& tokens) {
    std::vector<Run> runs;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind == TK_PULSE || tokens[i].kind == TK_DASH) {
            const TokenKind groupKind = tokens[i].kind;
            const uint32_t groupMs = tokens[i].ms;
            size_t runLen = 1;
            while (i + runLen < tokens.size()
                   && tokens[i + runLen].kind == groupKind
                   && tokens[i + runLen].ms == groupMs) {
                ++runLen;
            }
            runs.push_back({static_cast<uint32_t>(runLen), tokens[i]});
            i += runLen - 1;
        } else {
            runs.push_back({1, tokens[i]});
        }
    }
    return runs;
}

// Stage 4 — render the collapsed runs into the final comma-separated string.
// Each run becomes "PULSE Xs", "NxPULSE Xs", "ON Xs", or "OFF Xs".
// Local formatting shim (StatusLEDRenderer::formatDuration is private and must
// not be exposed via the header — SRP is a .cpp-local concern).
static std::string fmtDuration(uint32_t durationMs) {
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1);
    formatted << (durationMs / 1000.0) << "s";
    return formatted.str();
}

static std::string renderTimingRuns(const std::vector<Run>& runs) {
    std::ostringstream out;
    bool first = true;
    auto emitSep = [&]() { if (!first) out << ", "; first = false; };

    for (const Run& r : runs) {
        emitSep();
        if (r.count > 1) out << r.count << "x";
        switch (r.token.kind) {
            case TK_PULSE: out << "PULSE " << fmtDuration(r.token.ms); break;
            case TK_DASH:  out << "DASH "  << fmtDuration(r.token.ms); break;
            case TK_ON:    out << "ON "    << fmtDuration(r.token.ms); break;
            case TK_OFF:   out << "OFF "   << fmtDuration(r.token.ms); break;
        }
    }
    return out.str();
}

std::string StatusLEDRenderer::renderPattern(StatusLED::Pattern pattern) {
    auto [steps, stepCount] = StatusLED::getPatternSteps(pattern);
    if (stepCount == 0) return "";

    const bool isSingleState = (stepCount == 1);

    struct Run { LEDState state; uint32_t tenths; };
    std::vector<Run> plan;
    uint32_t totalTenths = 0;
    for (size_t i = 0; i < stepCount; ++i) {
        if (!isSingleState && isTrailingSeparator(steps[i], i + 1 == stepCount)) {
            continue;
        }
        const uint32_t tenths = steps[i].durationMs / 100;
        if (tenths == 0) continue;
        plan.push_back({steps[i].state, tenths});
        totalTenths += tenths;
    }
    if (totalTenths == 0) return "";

    std::ostringstream out;
    out << '|';

    size_t runIdx = 0;
    uint32_t runTenthsLeft = plan[0].tenths;
    uint32_t tenthsElapsed = 0;
    while (tenthsElapsed < totalTenths) {
        if (tenthsElapsed > 0 && tenthsElapsed % 10 == 0) {
            out << '|';
        }
        out << (plan[runIdx].state == LEDState::ON ? '-' : ' ');
        ++tenthsElapsed;
        if (--runTenthsLeft == 0 && runIdx + 1 < plan.size()) {
            ++runIdx;
            runTenthsLeft = plan[runIdx].tenths;
        }
    }

    out << '|';
    return out.str();
}

std::string StatusLEDRenderer::timingNote(StatusLED::Pattern pattern) {
    auto [steps, stepCount] = StatusLED::getPatternSteps(pattern);
    if (stepCount == 0) return "";

    // Single-state trivial case: a lone ON/OFF step is "solid ON"/"solid OFF".
    if (stepCount == 1) {
        return steps[0].state == LEDState::ON ? "Solid ON" : "Solid OFF";
    }

    // Multi-stage pipeline, each stage a focused helper (SRP):
    //   1. filterSteps     — drop the trailing separator
    //   2. tokenizePulses  — collapse symmetric ON/OFF pairs into PULSE tokens,
    //      and clearly-asymmetric (>=3x) pairs into DASH tokens
    //   3. collapsePulseRuns — merge N identical consecutive pulses/dashes into one run
    //   4. renderTimingRuns — format the runs to the final comma-separated string
    const std::vector<TimingStep> kept = filterSteps(steps, stepCount);
    if (kept.empty()) return "";
    const std::vector<Token> tokens = tokenizePulses(kept);
    const std::vector<Run> runs = collapsePulseRuns(tokens);
    return renderTimingRuns(runs);
}

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
        case StatusLED::Pattern::FATAL_UNRECOVERABLE_SOS: bare = "FATAL_UNRECOVERABLE_SOS"; break;
        case StatusLED::Pattern::OFF:                  bare = "OFF"; break;
    }
    return std::string{"StatusLED::Pattern::"} + (bare ? bare : "?");
}

static size_t maxPatternVisualWidth() {
    size_t maxWidth = 0;
    for (const auto& info : PATTERN_REGISTRY) {
        maxWidth = std::max(maxWidth, StatusLEDRenderer::renderPattern(info.pattern).size());
    }
    return maxWidth;
}

#ifdef INCLUDE_LED_HELP_KEY
static size_t maxConstantVisualWidth() {
    size_t maxWidth = 0;
    for (const auto& c : NAMED_CONSTANTS) {
        maxWidth = std::max(maxWidth, renderNamedConstant(c).size());
    }
    return maxWidth;
}
#endif

std::string StatusLEDRenderer::generateTable() {
    std::ostringstream out;

    out << "Status LED patterns (1 char = 100ms, |'s mark each whole second; "
        << "trailing 2s separator is omitted from each row)\n\n";

    const size_t visualWidth = maxPatternVisualWidth();
    size_t nameWidth = 0;
    for (const auto& info : PATTERN_REGISTRY) {
        nameWidth = std::max(nameWidth, std::strlen(info.name));
    }

    for (const auto& info : PATTERN_REGISTRY) {
        std::string visual = renderPattern(info.pattern);
        std::string note = timingNote(info.pattern);
        out << "  " << std::left << std::setw(static_cast<int>(nameWidth)) << info.name
            << "  " << std::left << std::setw(static_cast<int>(visualWidth)) << visual
            << "  # " << note << "\n";
    }

#ifdef INCLUDE_LED_HELP_KEY
    const size_t constVisualWidth = maxConstantVisualWidth();
    size_t constNameWidth = 0;
    for (const auto& c : NAMED_CONSTANTS) {
        constNameWidth = std::max(constNameWidth, std::strlen(c.name));
    }

    out << "\nKey:\n";
    for (const auto& c : NAMED_CONSTANTS) {
        const std::string cVisual = renderNamedConstantPadded(c, constVisualWidth);
        const std::string comment = (c.state == LEDState::ON ? "ON " : "OFF ")
            + formatDuration(c.durationMs);
        out << "  " << std::left << std::setw(static_cast<int>(constNameWidth)) << c.name
            << "  " << cVisual
            << "  # " << comment << "\n";
    }
#endif

    return out.str();
}

std::string StatusLEDRenderer::generateHelpText() {
    std::ostringstream help;

    help << "Status LED Patterns\n";
    help << "===================\n\n";
    help << "Visual key:\n";
    help << "  '-' = LED ON\n";
    help << "  ' ' = LED OFF\n";
    help << "  '|' = whole-second boundary (start, 1s, 2s, ...)\n";
    help << "  Trailing 2s separators are omitted from the visual.\n\n";
    help << "Each character represents 100ms. Patterns repeat continuously.\n\n";

    std::map<PatternCategory, std::vector<const PatternInfo*>> grouped;
    for (const auto& patternInfo : PATTERN_REGISTRY) {
        grouped[patternInfo.category].push_back(&patternInfo);
    }

    for (const auto& [category, patterns] : grouped) {
        help << "## " << getCategoryName(category) << "\n\n";

        for (const auto* patternInfo : patterns) {
            help << patternInfo->name << "\n";
            help << "  " << patternInfo->description << "\n";

            std::string visual = renderPattern(patternInfo->pattern);
            help << "  Visual: " << visual << "\n";

            auto [steps, stepCount] = StatusLED::getPatternSteps(patternInfo->pattern);
            if (stepCount > 0) {
                help << "  Timing: ";
                for (size_t i = 0; i < stepCount; ++i) {
                    const LEDStep& step = steps[i];
                    if (i > 0) help << ", ";
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

} // namespace firmware
