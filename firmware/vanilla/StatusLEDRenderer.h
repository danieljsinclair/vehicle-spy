#ifndef FIRMWARE_STATUS_LED_RENDERER_H
#define FIRMWARE_STATUS_LED_RENDERER_H

#include <string>
#include <map>
#include <vector>
#include "StatusLED.h"

namespace firmware {

// Pattern category for grouping in help text
enum class PatternCategory {
    BOOT,
    WIFI,
    CONNECTION,
    AP_MODE,
    OTA,
    ERROR,
    FATAL,
    OFF
};

// Pattern metadata for rendering and grouping
struct PatternInfo {
    StatusLED::Pattern pattern;
    PatternCategory category;
    const char* name;           // Display name
    const char* description;     // Human-readable description
};

// StatusLED pattern renderer - DRY, single-source-of-truth visual representation
// Renders StatusLED patterns as visual strings for CLI help output
class StatusLEDRenderer {
public:
    // Render a single pattern to a visual string
    // One character per 100ms: '-' for ON, ' ' for OFF, '#' for SOLID, '|' for SEPARATOR
    static std::string renderPattern(StatusLED::Pattern pattern);

    // Generate formatted help text showing all patterns with visual representation
    // Groups patterns logically (WiFi states together, errors together, etc.)
    static std::string generateHelpText();

    // Compact diagnostic table: one pattern per line ("NAME  <visual>  # note").
    // Backs the --led-diag CLI flag.
    static std::string generateTable();

private:
    // Get pattern metadata (name, category, description)
    static PatternInfo getPatternInfo(StatusLED::Pattern pattern);

    // Get category name for display
    static const char* getCategoryName(PatternCategory category);

    // Format duration as human-readable string (e.g., "0.1s on, 0.9s off")
    static std::string formatDuration(uint32_t durationMs);

    // Pattern registry for iteration
    static const std::vector<PatternInfo> getAllPatterns();

    // Brief inline timing note for a pattern (e.g. "ON 0.1s, OFF 0.9s" / "solid ON").
    static std::string timingNote(StatusLED::Pattern pattern);
};

} // namespace firmware

#endif // FIRMWARE_STATUS_LED_RENDERER_H
