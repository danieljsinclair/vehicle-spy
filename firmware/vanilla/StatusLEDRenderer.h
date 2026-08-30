#ifndef FIRMWARE_STATUS_LED_RENDERER_H
#define FIRMWARE_STATUS_LED_RENDERER_H

#include <string>
#include <map>
#include <vector>
#include <cstring>
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
    // One character per 100ms: '-' for ON, ' ' for OFF, '#' for SOLID ON, '.' for
    // SOLID OFF, '|' as a divider at every whole-second boundary.
    static std::string renderPattern(StatusLED::Pattern pattern);

    // Generate formatted help text showing all patterns with visual representation
    // Groups patterns logically (WiFi states together, errors together, etc.)
    static std::string generateHelpText();

    // Compact diagnostic table: one pattern per line ("NAME  <visual>  # note").
    // Backs the --led-help CLI flag.
    static std::string generateTable();

    // Fully-qualified C++ enum name for a pattern (e.g.
    // "StatusLED::Pattern::WIFI_SEARCHING"). Used by generateTable() to print
    // the greppable enum-spelling column.
    static std::string enumName(StatusLED::Pattern pattern);

private:
    // Get category name for display
    static const char* getCategoryName(PatternCategory category);

    // Format duration as human-readable string (e.g., "0.1s on, 0.9s off")
    static std::string formatDuration(uint32_t durationMs);

    // Brief inline timing note for a pattern (e.g. "ON 0.1s, OFF 0.9s" / "solid ON").
    static std::string timingNote(StatusLED::Pattern pattern);
};

} // namespace firmware

#endif // FIRMWARE_STATUS_LED_RENDERER_H
