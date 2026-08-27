// StatusLEDRenderer_test.cpp - Tests for StatusLED pattern renderer

#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include "vanilla/StatusLED.h"
#include "vanilla/StatusLEDRenderer.h"

using namespace firmware;

// Rendered output tests — divider-aligned visuals. One '|' divider at every
// whole-second boundary (start, end, each whole second). One character per
// 100ms: '-' ON, ' ' OFF, '#' solid ON, '.' solid OFF.
TEST(StatusLEDRendererTest, RenderPattern_WifiSearching) {
    // WIFI_SEARCHING: ON 100ms + OFF 900ms (1s total)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::WIFI_SEARCHING);
    EXPECT_EQ(rendered, "|-         |");  // 1 dash, 9 spaces, 2 dividers (0s/1s)
}

TEST(StatusLEDRendererTest, RenderPattern_WifiConnected) {
    // WIFI_CONNECTED: ON 800ms + OFF 200ms (1s total)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::WIFI_CONNECTED);
    EXPECT_EQ(rendered, "|--------  |");  // 8 dashes, 2 spaces, 2 dividers
}

TEST(StatusLEDRendererTest, RenderPattern_ClientConnected) {
    // CLIENT_CONNECTED (solid, 2s)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::CLIENT_CONNECTED);
    EXPECT_EQ(rendered, "|##########|##########|");  // 20 hashes split by 1s divider; 3 dividers
}

TEST(StatusLEDRendererTest, RenderPattern_Boot) {
    // BOOT: ON 500ms + OFF 500ms (1s total)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::BOOT);
    EXPECT_EQ(rendered, "|-----     |");  // 5 dashes, 5 spaces, 2 dividers (0s/1s)
}

TEST(StatusLEDRendererTest, RenderPattern_Off) {
    // OFF (solid, 2s)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::OFF);
    EXPECT_EQ(rendered, "|..........|..........|");  // 20 dots split by 1s divider; 3 dividers
}

TEST(StatusLEDRendererTest, RenderPattern_ApMode) {
    // AP_MODE: ON 800, OFF 100, ON 100, OFF 100, ON 100, OFF 2000 (3.2s)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::AP_MODE);
    EXPECT_EQ(rendered, "|-------- -| -        |          |  |");
}

TEST(StatusLEDRendererTest, RenderPattern_OtaInProgress) {
    // OTA_IN_PROGRESS: SHORT_FLASH ON 200ms + SHORT_GAP OFF 200ms (400ms total)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::OTA_IN_PROGRESS);
    EXPECT_EQ(rendered, "|--  |");  // 2 dashes, 2 spaces, 2 dividers (0s/0.4s)
}

TEST(StatusLEDRendererTest, RenderPattern_AuthFailure) {
    // ERROR_AUTH_FAILURE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 2×(TINY_ON 100 + TINY_OFF 100) + SEP 2000
    // 3.6s total
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_AUTH_FAILURE);
    EXPECT_EQ(rendered, "|--  --  --|  - -     |          |      |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorRecoverable) {
    // ERROR_RECOVERABLE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 3×(TINY_ON 100 + TINY_OFF 100) + SEP 2000
    // 3.8s total
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_RECOVERABLE);
    EXPECT_EQ(rendered, "|--  --  --|  - - -   |          |        |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorNoNtpService) {
    // ERROR_NO_NTP_SERVICE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 1×(TINY_ON 100 + TINY_OFF 100) + SEP 2000
    // 3.4s total
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    EXPECT_EQ(rendered, "|--  --  --|  -       |          |    |");
}

TEST(StatusLEDRendererTest, RenderPattern_FatalUnrecoverable) {
    // FATAL_UNRECOVERABLE: SOS — 3×(SHORT_ON 200 + SHORT_OFF 200) + 3×(LONG_ON 800 + SHORT_OFF 200) +
    //                       3×(SHORT_ON 200 + SHORT_OFF 200) + SEP 2000
    // 7.4s total
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::FATAL_UNRECOVERABLE);
    EXPECT_EQ(rendered,
              "|--  --  --|  --------|  --------|  --------|  --  --  |--        |          |    |");
}

TEST(StatusLEDRendererTest, GenerateHelpText_ContainsAllPatterns) {
    std::string help = StatusLEDRenderer::generateHelpText();

    // Verify help text contains all pattern names
    EXPECT_NE(help.find("BOOT"), std::string::npos);
    EXPECT_NE(help.find("WIFI_SEARCHING"), std::string::npos);
    EXPECT_NE(help.find("WIFI_CONNECTED"), std::string::npos);
    EXPECT_NE(help.find("CLIENT_CONNECTED"), std::string::npos);
    EXPECT_NE(help.find("AP_MODE"), std::string::npos);
    EXPECT_NE(help.find("OTA_IN_PROGRESS"), std::string::npos);
    EXPECT_NE(help.find("ERROR_AUTH_FAILURE"), std::string::npos);
    EXPECT_NE(help.find("ERROR_RECOVERABLE"), std::string::npos);
    EXPECT_NE(help.find("ERROR_NO_NTP_SERVICE"), std::string::npos);
    EXPECT_NE(help.find("FATAL_UNRECOVERABLE"), std::string::npos);
    EXPECT_NE(help.find("OFF"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateHelpText_GroupsPatternsLogically) {
    std::string help = StatusLEDRenderer::generateHelpText();

    // WiFi states should be grouped together
    size_t wifiSearching = help.find("WIFI_SEARCHING");
    size_t wifiConnected = help.find("WIFI_CONNECTED");

    // They should appear relatively close (within reasonable distance)
    EXPECT_NE(wifiSearching, std::string::npos);
    EXPECT_NE(wifiConnected, std::string::npos);

    // Error states should be grouped together
    size_t authFailure = help.find("ERROR_AUTH_FAILURE");
    size_t errorRecoverable = help.find("ERROR_RECOVERABLE");
    size_t errorNoNtp = help.find("ERROR_NO_NTP_SERVICE");
    size_t fatalUnrecoverable = help.find("FATAL_UNRECOVERABLE");

    EXPECT_NE(authFailure, std::string::npos);
    EXPECT_NE(errorRecoverable, std::string::npos);
    EXPECT_NE(errorNoNtp, std::string::npos);
    EXPECT_NE(fatalUnrecoverable, std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateHelpText_ContainsHumanReadableNotes) {
    std::string help = StatusLEDRenderer::generateHelpText();

    // Should contain human-readable timing notes
    EXPECT_NE(help.find("0.1s"), std::string::npos);  // TINY_FLASH
    EXPECT_NE(help.find("0.2s"), std::string::npos);  // SHORT_FLASH
    EXPECT_NE(help.find("0.5s"), std::string::npos);  // MED_FLASH
    EXPECT_NE(help.find("0.8s"), std::string::npos);  // LONG_FLASH
}

TEST(StatusLEDRendererTest, GenerateHelpText_ContainsVisualRepresentation) {
    std::string help = StatusLEDRenderer::generateHelpText();

    // Should contain visual indicators
    EXPECT_NE(help.find("-"), std::string::npos);  // ON indicator
    EXPECT_NE(help.find(" "), std::string::npos);  // OFF indicator
}

TEST(StatusLEDRendererTest, GenerateHelpText_OutputStructure) {
    std::string help = StatusLEDRenderer::generateHelpText();

    // Verify basic structure elements
    EXPECT_NE(help.find("Status LED Patterns"), std::string::npos);
    EXPECT_NE(help.find("Visual key:"), std::string::npos);
    EXPECT_NE(help.find("BOOT"), std::string::npos);

    // Verify visual patterns are present for key states (divider-aligned).
    EXPECT_NE(help.find("|-         |"), std::string::npos);  // WIFI_SEARCHING pattern
    EXPECT_NE(help.find("|--------  |"), std::string::npos);  // WIFI_CONNECTED pattern
    EXPECT_NE(help.find("##########"), std::string::npos);  // CLIENT_CONNECTED (subset of 20 #'s)

    // Verify timing notes are present
    EXPECT_NE(help.find("0.1s"), std::string::npos);  // TINY_FLASH
    EXPECT_NE(help.find("0.8s"), std::string::npos);  // LONG_FLASH
}

// generateTable: the compact one-line-per-pattern diagnostic table that backs
// the --led-help CLI flag. Distinct from generateHelpText() (already tested).
// Drives generateTable() + getCategoryName() (all category cases, reached via
// the category grouping) + timingNote() (incl. the SEPARATOR branch via the
// AP_MODE / error / fatal patterns) and formatDuration(). (getPatternInfo /
// getAllPatterns are NOT exercised here — they are dead code with no callers,
// flagged separately for tech-arch triage.)
TEST(StatusLEDRendererTest, GenerateTable_ContainsEveryPatternName) {
    std::string table = StatusLEDRenderer::generateTable();

    // Every registered pattern name must appear as a row.
    EXPECT_NE(table.find("OFF"), std::string::npos);
    EXPECT_NE(table.find("BOOT"), std::string::npos);
    EXPECT_NE(table.find("WIFI_SEARCHING"), std::string::npos);
    EXPECT_NE(table.find("WIFI_CONNECTED"), std::string::npos);
    EXPECT_NE(table.find("CLIENT_CONNECTED"), std::string::npos);
    EXPECT_NE(table.find("AP_MODE"), std::string::npos);
    EXPECT_NE(table.find("OTA_IN_PROGRESS"), std::string::npos);
    EXPECT_NE(table.find("ERROR_AUTH_FAILURE"), std::string::npos);
    EXPECT_NE(table.find("ERROR_RECOVERABLE"), std::string::npos);
    EXPECT_NE(table.find("ERROR_NO_NTP_SERVICE"), std::string::npos);
    EXPECT_NE(table.find("FATAL_UNRECOVERABLE"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_HasHeaderAndTimingNotes) {
    std::string table = StatusLEDRenderer::generateTable();

    // Header explains the encoding (intent, not exact wording).
    EXPECT_NE(table.find("100ms"), std::string::npos);
    // Timing-note column ("# ...") carries formatted durations.
    EXPECT_NE(table.find("0.1s"), std::string::npos);
    EXPECT_NE(table.find("0.8s"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_RendersSolidOnAndSeparatorTiming) {
    std::string table = StatusLEDRenderer::generateTable();

    // CLIENT_CONNECTED is a solid-ON pattern -> "solid ON" timing note, with
    // the '#' (solid) glyph in its visual.
    EXPECT_NE(table.find("solid ON"), std::string::npos);
    // Patterns carrying a SEPARATOR step (AP_MODE / error / fatal families)
    // render a "sep" timing entry — exercises the SEPARATOR branch of
    // timingNote(), which generateHelpText() does not reliably reach.
    EXPECT_NE(table.find("sep"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_GroupsPatternsAdjacently) {
    std::string table = StatusLEDRenderer::generateTable();

    // generateTable groups by PatternCategory (via std::map) but does NOT emit
    // category headers (that line is intentionally commented out — header
    // rendering lives in generateHelpText). What we CAN assert: members of
    // the same category appear adjacently. The two WIFI patterns are consecutive.
    const size_t searching = table.find("WIFI_SEARCHING");
    const size_t connected = table.find("WIFI_CONNECTED");
    ASSERT_NE(searching, std::string::npos);
    ASSERT_NE(connected, std::string::npos);
    EXPECT_NE(searching, connected);

    // No other registered pattern name should appear between them (adjacency).
    static const std::string kOtherPatterns[] = {
        "BOOT", "CLIENT_CONNECTED", "AP_MODE", "OTA_IN_PROGRESS",
        "ERROR_AUTH_FAILURE", "ERROR_RECOVERABLE", "ERROR_NO_NTP_SERVICE",
        "FATAL_UNRECOVERABLE",
    };
    const size_t lo = std::min(searching, connected);
    const size_t hi = std::max(searching, connected);
    for (const std::string& name : kOtherPatterns) {
        const size_t pos = table.find(name);
        ASSERT_NE(pos, std::string::npos);
        // Each non-WiFi pattern must sit outside the [lo,hi] window.
        EXPECT_TRUE(pos < lo || pos > hi)
            << "pattern '" << name << "' splits the WiFi group";
    }
}

// Divider-alignment contract: every rendered visual starts AND ends with '|',
// and contains a '|' at every whole-second boundary of the pattern's period.
// Together with the 1-char-per-100ms rule, this makes 0.8s flashes, 1s
// patterns, and 2s separators line up across the table.
TEST(StatusLEDRendererTest, RenderPattern_HasStartAndEndDividers) {
    const StatusLED::Pattern all[] = {
        StatusLED::Pattern::BOOT, StatusLED::Pattern::WIFI_SEARCHING,
        StatusLED::Pattern::WIFI_CONNECTED, StatusLED::Pattern::CLIENT_CONNECTED,
        StatusLED::Pattern::AP_MODE, StatusLED::Pattern::OTA_IN_PROGRESS,
        StatusLED::Pattern::ERROR_AUTH_FAILURE, StatusLED::Pattern::ERROR_RECOVERABLE,
        StatusLED::Pattern::ERROR_NO_NTP_SERVICE,
        StatusLED::Pattern::FATAL_UNRECOVERABLE,
        StatusLED::Pattern::OFF
    };
    for (auto p : all) {
        std::string r = StatusLEDRenderer::renderPattern(p);
        ASSERT_FALSE(r.empty()) << "empty render";
        EXPECT_EQ(r.front(), '|') << "missing start divider";
        EXPECT_EQ(r.back(), '|') << "missing end divider";
    }
}

TEST(StatusLEDRendererTest, RenderPattern_DividersAtEveryWholeSecond) {
    // 2s solid pattern (CLIENT_CONNECTED) must contain exactly 3 dividers
    // (start, 1s, 2s-end) and the middle divider is 11 chars from the start.
    std::string r = StatusLEDRenderer::renderPattern(StatusLED::Pattern::CLIENT_CONNECTED);
    size_t count = 0;
    for (char c : r) if (c == '|') ++count;
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(r[11], '|');
}

TEST(StatusLEDRendererTest, GenerateTable_ContainsEnumNames) {
    // The table must surface fully-qualified enum spellings so users can
    // grep from CLI output to source.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("StatusLED::Pattern::WIFI_SEARCHING"), std::string::npos);
    EXPECT_NE(table.find("StatusLED::Pattern::FATAL_UNRECOVERABLE"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_ContainsKeyExplainingDividers) {
    // Key at the bottom of the table documents the '|' divider semantics.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("whole second"), std::string::npos);
    EXPECT_NE(table.find("100ms"), std::string::npos);
}

TEST(StatusLEDRendererTest, EnumName_ReturnsQualifiedSpelling) {
    // enumName is the data source for the enum-name column in generateTable.
    EXPECT_EQ(StatusLEDRenderer::enumName(StatusLED::Pattern::BOOT),
              "StatusLED::Pattern::BOOT");
    EXPECT_EQ(StatusLEDRenderer::enumName(StatusLED::Pattern::CLIENT_CONNECTED),
              "StatusLED::Pattern::CLIENT_CONNECTED");
}
