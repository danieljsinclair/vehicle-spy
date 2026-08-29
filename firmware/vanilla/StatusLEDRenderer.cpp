#include "StatusLEDRenderer.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

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

// ── Key-section helpers ───────────────────────────────────────────────────────
// The Key section renders a fixed four-entry legend explaining how to read the
// per-pattern timing notes. It is ALWAYS emitted (no INCLUDE_LED_HELP_KEY guard
// any more) — see generateTable(). Each entry shows a quoted visual bar, then
// a comment naming the token it produces.

// A plain visual bar of N identical chars (no surrounding dividers): used for
// the FLASH / DOT / DASH entries where the bar is just the raw ON/OFF run.
static std::string visualBar(char glyph, uint32_t tenths) {
    return std::string(tenths, glyph);
}

// The 2s SEPARATOR bar: a 2-second OFF shown with the whole-second divider in
// the middle (10 spaces, '|', 10 spaces) — mirrors how renderPattern() draws a
// 2s solid-OFF pattern, but without the leading/trailing dividers.
static std::string separatorBar() {
    return std::string(10, ' ') + '|' + std::string(10, ' ');
}

// A quoted, padded visual for a Key entry: "'" + bar + "'", left-padded to
// width so the '#' comments column-align.
static std::string quotedVisual(const std::string& bar, size_t width) {
    const std::string quoted = "'" + bar + "'";
    if (quoted.size() >= width) return quoted;
    return quoted + std::string(width - quoted.size(), ' ');
}

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

// True if the pattern carries a 2s SEPARATOR step anywhere. A separator is
// either an explicit LEDState::SEPARATOR step, or an OFF step lasting >=
// SEPARATOR_MS — but only in a MULTI-STEP pattern: single-state solid patterns
// (CLIENT_CONNECTED, OFF) reuse SEPARATOR_MS as their cycle duration without
// it being a meaningful separator gap, so they are excluded. Position-blind —
// a separator anywhere in the pattern qualifies, not just a trailing one.
static bool hasSeparatorAnywhere(const LEDStep* steps, size_t stepCount) {
    if (stepCount <= 1) return false;
    for (size_t i = 0; i < stepCount; ++i) {
        if (steps[i].state == LEDState::SEPARATOR) return true;
        if (steps[i].state == LEDState::OFF
            && steps[i].durationMs >= StatusLEDConstants::SEPARATOR_MS) {
            return true;
        }
    }
    return false;
}

// ── timingNote() helpers ───────────────────────────────────────────────────
// timingNote() renders a pattern's step sequence as a compact human-readable
// string (e.g. "PULSE 0.5s", "FLASH, OFF 0.9s", "DOT, DOT, DOT, FLASH, FLASH").
// The helpers below split that transformation into single-responsibility stages
// so the public method reads top-to-bottom as prose.
//
// RENDERING RULES (classify each contiguous ON run by its visual width; each
// char = 100ms):
//   * 1 char  ON            → "FLASH"
//   * 2 chars ON            → "DOT"
//   * 3+ chars ON           → "DASH <dur>", UNLESS the following OFF gap is
//                             equal duration (symmetric pair) → "PULSE <dur>"
//   * OFF gap ≥ 3× the preceding ON → "OFF <dur>" (a dominant rest; otherwise
//                             the inter-element gap is omitted).
// No run-collapse: three consecutive PULSE 0.5s tokens render long-hand as
// "PULSE 0.5s, PULSE 0.5s, PULSE 0.5s" (KISS — never "3xPULSE 0.5s").

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

// Stage 2 — classify a single ON step by visual width into FLASH / DOT / DASH.
// 1 char (100ms) → FLASH, 2 chars (200ms) → DOT, 3+ chars (≥300ms) → DASH.
// Pure classification; PULSE/ OFF handling lives in the scan (Stage 3).
enum TokenKind { TK_FLASH, TK_DOT, TK_DASH, TK_PULSE, TK_OFF };
struct Token { TokenKind kind; uint32_t ms; };

static Token classifyOn(uint32_t onMs) {
    if (onMs <= 100) return {TK_FLASH, onMs};
    if (onMs <= 200) return {TK_DOT, onMs};
    return {TK_DASH, onMs};
}

// Stage 3 — scan the filtered steps and emit one token per ON run (FLASH/DOT/
// DASH), folding a symmetric ON/OFF pair into a PULSE, and emitting a dominant
// OFF gap (≥ 3× the preceding ON) as an OFF token. Inter-element gaps shorter
// than that are omitted — they are the "space between blips", not meaningful
// rests. Returns the token list in order (long-hand, no run-collapse).
static std::vector<Token> tokenize(const std::vector<TimingStep>& steps) {
    std::vector<Token> tokens;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].state != LEDState::ON) continue;  // OFF handled inline below
        const uint32_t onMs = steps[i].ms;
        const bool hasFollowingOff = (i + 1 < steps.size())
            && steps[i + 1].state == LEDState::OFF;
        const uint32_t offMs = hasFollowingOff ? steps[i + 1].ms : 0;

        // 3+ char ON with a symmetric (equal) following OFF → PULSE.
        if (onMs >= 300 && hasFollowingOff && offMs == onMs) {
            tokens.push_back({TK_PULSE, onMs});
            ++i;  // consume the matching OFF
        } else {
            tokens.push_back(classifyOn(onMs));
        }

        // A dominant OFF rest (≥ 3× the ON that preceded it) is shown;
        // shorter inter-element gaps are silently dropped.
        if (hasFollowingOff && offMs >= 3 * onMs) {
            tokens.push_back({TK_OFF, offMs});
        }
    }
    return tokens;
}

// Stage 4 — render the token list into the final comma-separated string, one
// token at a time (long-hand: no Nx collapse). Local formatting shim
// (StatusLEDRenderer::formatDuration is private and must not be exposed via the
// header — SRP is a .cpp-local concern).
static std::string fmtDuration(uint32_t durationMs) {
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1);
    formatted << (durationMs / 1000.0) << "s";
    return formatted.str();
}

static std::string renderTokens(const std::vector<Token>& tokens) {
    std::ostringstream out;
    bool first = true;
    auto emitSep = [&]() { if (!first) out << ", "; first = false; };

    for (const Token& tok : tokens) {
        emitSep();
        switch (tok.kind) {
            case TK_FLASH: out << "FLASH"; break;
            case TK_DOT:   out << "DOT";   break;
            case TK_DASH:  out << "DASH "  << fmtDuration(tok.ms); break;
            case TK_PULSE: out << "PULSE " << fmtDuration(tok.ms); break;
            case TK_OFF:   out << "OFF "   << fmtDuration(tok.ms); break;
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
    //   1. filterSteps  — drop the trailing separator
    //   2. tokenize     — classify each ON run by width (FLASH/DOT/DASH), fold
    //      symmetric ON/OFF pairs into PULSE, emit dominant OFF rests
    //   3. renderTokens — format the tokens long-hand (no Nx collapse)
    const std::vector<TimingStep> kept = filterSteps(steps, stepCount);
    if (kept.empty()) return "";
    const std::vector<Token> tokens = tokenize(kept);
    return renderTokens(tokens);
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
        // Annotate the comment when the pattern carries a 2s SEPARATOR anywhere
        // (AP_MODE / error / fatal families). The trailing separator is omitted
        // from the visual, so the label makes the gap's role self-documenting.
        auto [steps, stepCount] = StatusLED::getPatternSteps(info.pattern);
        if (hasSeparatorAnywhere(steps, stepCount)) {
            note += ", 2s SEPARATOR";
        }
        out << "  " << std::left << std::setw(static_cast<int>(nameWidth)) << info.name
            << "  " << std::left << std::setw(static_cast<int>(visualWidth)) << visual
            << "  # " << note << "\n";
    }

    // Key: a fixed four-entry legend mapping each visual bar to the timing
    // token it produces. Always rendered (no INCLUDE_LED_HELP_KEY guard). The
    // quoted visual bars column-align so the '#' comments line up.
    //
    // Entries (visual → token):
    //   '-'            → FLASH 0.1s
    //   '--'           → DOT 0.2s
    //   '--------'     → DASH 0.8s
    //   '        |   ' → 2s SEPARATOR
    struct KeyEntry { std::string bar; const char* comment; };
    static const std::vector<KeyEntry> KEY_ENTRIES = {
        {visualBar('-', 1),  "FLASH 0.1s"},
        {visualBar('-', 2),  "DOT 0.2s"},
        {visualBar('-', 8),  "DASH 0.8s"},
        {separatorBar(),     "2s SEPARATOR"},
    };

    // Column-align the comments: find the widest quoted visual.
    size_t keyVisualWidth = 0;
    for (const auto& e : KEY_ENTRIES) {
        keyVisualWidth = std::max(keyVisualWidth, std::string("'").size()
            + e.bar.size() + std::string("'").size());
    }

    out << "\nKey;\n";
    for (const auto& e : KEY_ENTRIES) {
        out << "  " << quotedVisual(e.bar, keyVisualWidth)
            << "  # " << e.comment << "\n";
    }

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
