#ifndef FIRMWARE_STATUS_LED_H
#define FIRMWARE_STATUS_LED_H

#include "IStatusLEDOutput.h"
#include "WiFiManager.h"  // For esp32_firmware::IStatusLED interface (symlinked from vanilla/)
#include <cstdint>
#include <array>

namespace firmware {

// ── Named Flash and Gap Constants (NO magic numbers - DRY) ─────────────────────
// Per user spec: exact named constants for all timing values
struct StatusLEDConstants {
    // Flash durations (milliseconds)
    static constexpr uint32_t TINY_FLASH_MS   = 100;   // 0.1s
    static constexpr uint32_t SHORT_FLASH_MS  = 200;   // 0.2s
    static constexpr uint32_t MED_FLASH_MS    = 500;   // 0.5s
    static constexpr uint32_t LONG_FLASH_MS   = 800;   // 0.8s
    static constexpr uint32_t VERY_LONG_FLASH_MS = 1800; // 1.8s

    // Gap durations (milliseconds)
    static constexpr uint32_t TINY_GAP_MS     = 100;   // 0.1s
    static constexpr uint32_t SHORT_GAP_MS    = 200;   // 0.2s
    static constexpr uint32_t MED_GAP_MS      = 500;   // 0.5s
    static constexpr uint32_t LONG_GAP_MS     = 800;   // 0.8s
    static constexpr uint32_t VERY_LONG_GAP_MS = 2000; // 2s
    static constexpr uint32_t SEPARATOR_MS     = 2000; // 2s (may diverge from VERY_LONG_GAP_MS later)

    // Special gap for WIFI_SEARCHING (0.9s off per user spec)
    static constexpr uint32_t SEARCHING_GAP_MS = 900;   // 0.9s
};

// ── Opcode Structure for Declarative Patterns ───────────────────────────────
// Patterns are data, not code - defined as arrays of opcodes
enum class LEDState : uint8_t {
    ON,
    OFF,
    SEPARATOR  // Long pause before pattern repeat
};

struct LEDStep {
    LEDState state;
    uint32_t durationMs;
};

// ── Status LED State Machine (declarative, non-blocking, millis-based) ───────
class StatusLED : public esp32_firmware::IStatusLED {
public:
    // LED pattern enumeration
    enum class Pattern {
        OFF,                  // LED off
        BOOT,                 // MED_FLASH ON, MED_GAP OFF (cycles)
        WIFI_SEARCHING,       // TINY_FLASH ON, SEARCHING_GAP OFF (0.1s on, 0.9s off)
        WIFI_CONNECTED,       // LONG_FLASH ON, SHORT_GAP OFF (0.8s on, 0.2s off)
        CLIENT_CONNECTED,     // SOLID ON (no cycling)
        AP_MODE,              // LONG_FLASH ON, TINY_GAP OFF, TINY_FLASH ON, TINY_GAP OFF, TINY_FLASH ON, SEPARATOR
        OTA_IN_PROGRESS,      // SHORT_FLASH ON, SHORT_GAP OFF (0.2s on/off rapid)
        ERROR_AUTH_FAILURE,         // ERROR_3_PULSE + 2×TINY_PULSE + SEPARATOR
        ERROR_RECOVERABLE,    // ERROR_3_PULSE + 3×TINY_PULSE + SEPARATOR
        ERROR_NO_NTP_SERVICE, // ERROR_3_PULSE + 1×TINY_PULSE + SEPARATOR
        FATAL_UNRECOVERABLE   // SOS: 3×SHORT, 3×LONG, 3×SHORT, SEPARATOR (morse SOS)
    };

    // Constructor with dependency injection for testability
    explicit StatusLED(IStatusLEDOutput* output);

    // Initialize the LED hardware and set initial pattern
    void init();

    // IStatusLED interface implementation
    void setPattern(int pattern) override;
    void update(uint32_t now) override;

    // Public API for Pattern enum (convenience overload)
    void setPattern(Pattern pattern) {
        setPattern(static_cast<int>(pattern));
    }

    // Get the current pattern
    Pattern getPattern() const { return currentPattern_; }

    // Test helper: Get pattern array for given pattern (exposed for white-box testing)
    static std::pair<const LEDStep*, size_t> getPatternSteps(Pattern pattern);

    // Diagnostic: human-readable name for a pattern. Exposed for host
    // testability (pure lookup, no side effects) — mirrors the
    // getPatternSteps precedent above.
    const char* getPatternName(Pattern pattern);

    // ── Pure pattern selector: (wifiState, clientConnected) -> Pattern ───────────
    // The SINGLE source of truth for LED pattern selection, replacing the two
    // parallel per-loop drivers (WiFiManager::applyStateTransition and
    // TcpServerManager::cycle) that raced on setPattern() with last-writer-wins.
    //
    // Priority rule (kills the race): when a TCP client is connected, the LED
    // shows CLIENT_CONNECTED regardless of the WiFi state — the client is the
    // user's active session and must take visual priority over WiFi searching.
    //
    // When no client is connected, the pattern reflects the WiFi state:
    //   DISCONNECTED   -> WIFI_SEARCHING  (looking for a network)
    //   CONNECTING     -> WIFI_SEARCHING  (in progress)
    //   CONNECTED_STA  -> WIFI_CONNECTED  (station mode, ready)
    //   CONNECTED_AP   -> AP_MODE         (access point mode)
    //   RECONNECTING   -> WIFI_SEARCHING  (recovering)
    //   (any other)    -> WIFI_SEARCHING  (fail-safe default)
    //
    // wifiState is an int (WiFiState::State ordinal) to keep this method
    // decoupled from the esp32_firmware namespace — StatusLED lives in the
    // firmware namespace and should not depend on WiFiManager.h.
    static Pattern selectLedPattern(int wifiState, bool clientConnected);

private:
    IStatusLEDOutput* output_;     // Hardware interface (DI)
    Pattern currentPattern_;       // Active pattern
    Pattern lastPattern_;          // Pattern from last update (for change detection)
    uint32_t lastUpdateTime_;     // Last time update() was called
    uint32_t phaseStartTime_;     // When current phase started
    uint32_t stepIndex_;          // Current step in pattern array
    bool ledOn_;                   // Current LED state

    // Internal implementation methods (called by interface and public API)
    void setPatternInternal(Pattern pattern);
    void updateInternal(uint32_t currentTime);

    // Helper to set LED state
    void setLedOn(bool on);

    // Reset pattern to beginning (on pattern change or cycle completion)
    void resetPattern(uint32_t currentTime);
};

} // namespace firmware

#endif // FIRMWARE_STATUS_LED_H
