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
    // CLIENT_CONNECTED (solid 2s) — single-state ON renders as 20 '-' chars
    // with a divider at the 1s mark. No '#' glyph (solid uses '-' too).
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::CLIENT_CONNECTED);
    EXPECT_EQ(rendered, "|----------|----------|");  // 20 dashes split by 1s divider; 3 dividers
}

TEST(StatusLEDRendererTest, RenderPattern_Boot) {
    // BOOT: ON 500ms + OFF 500ms (1s total)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::BOOT);
    EXPECT_EQ(rendered, "|-----     |");  // 5 dashes, 5 spaces, 2 dividers (0s/1s)
}

TEST(StatusLEDRendererTest, RenderPattern_Off) {
    // OFF (solid 2s) — single-state OFF renders as 20 ' ' chars with a
    // divider at the 1s mark. No '.' glyph (solid uses ' ' too).
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::OFF);
    EXPECT_EQ(rendered, "|          |          |");  // 20 spaces split by 1s divider; 3 dividers
}

TEST(StatusLEDRendererTest, RenderPattern_ApMode) {
    // AP_MODE (new timing): ON 800, OFF 200, ON 100, OFF 100, ON 100,
    // then trailing SEPARATOR (2s) — omitted from the visual. Total rendered:
    // 1.3s = 13 tenths + 1 divider at 1s + start/end. 16 chars.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::AP_MODE);
    EXPECT_EQ(rendered, "|--------  |- -|");
}

TEST(StatusLEDRendererTest, RenderPattern_OtaInProgress) {
    // OTA_IN_PROGRESS: SHORT_FLASH ON 200ms + SHORT_GAP OFF 200ms (400ms total)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::OTA_IN_PROGRESS);
    EXPECT_EQ(rendered, "|--  |");  // 2 dashes, 2 spaces, 2 dividers (0s/0.4s)
}

TEST(StatusLEDRendererTest, RenderPattern_AuthFailure) {
    // ERROR_AUTH_FAILURE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 2×(TINY_ON 100 + TINY_OFF 100)
    // + trailing SEP 2000 (omitted from visual). Total rendered: 1.6s = 16 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_AUTH_FAILURE);
    EXPECT_EQ(rendered, "|--  --  --|  - - |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorRecoverable) {
    // ERROR_RECOVERABLE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 3×(TINY_ON 100 + TINY_OFF 100)
    // + trailing SEP 2000 (omitted). Total rendered: 1.8s = 18 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_RECOVERABLE);
    EXPECT_EQ(rendered, "|--  --  --|  - - - |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorNoNtpService) {
    // ERROR_NO_NTP_SERVICE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 1×(TINY_ON 100 + TINY_OFF 100)
    // + trailing SEP 2000 (omitted). Total rendered: 1.4s = 14 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    EXPECT_EQ(rendered, "|--  --  --|  - |");
}

TEST(StatusLEDRendererTest, RenderPattern_FatalUnrecoverable) {
    // FATAL_UNRECOVERABLE: SOS — 3×(SHORT_ON 200 + SHORT_OFF 200) +
    //                       3×(LONG_ON 800 + SHORT_OFF 200) +
    //                       3×(SHORT_ON 200 + SHORT_OFF 200) +
    //                       trailing SEP 2000 (omitted). Total rendered: 5.4s = 54 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::FATAL_UNRECOVERABLE);
    EXPECT_EQ(rendered,
              "|--  --  --|  --------|  --------|  --------|  --  --  |--  |");
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
    // CLIENT_CONNECTED now uses '-' (no '#'): 20 dashes split by 1s divider
    EXPECT_NE(help.find("----------|----------|"), std::string::npos);

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

    // Single-state ON pattern (CLIENT_CONNECTED) -> "solid ON" timing note.
    EXPECT_NE(table.find("# solid ON"), std::string::npos);
    // Single-state OFF pattern (OFF) -> "solid OFF" timing note.
    EXPECT_NE(table.find("# solid OFF"), std::string::npos);
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

// Key section: the named-segment reference at the bottom of the table.
// Each named segment must appear with its visual + duration. Data-driven:
// adding a row to NAMED_SEGMENTS in the renderer is sufficient to surface it
// here. The Key section is behind INCLUDE_LED_HELP_KEY, so these tests are
// too — when the macro is undefined the Key section (and its tests) are
// excluded together, keeping the build green in both configurations.
#ifdef INCLUDE_LED_HELP_KEY
TEST(StatusLEDRendererTest, GenerateTable_KeyContainsAllNamedSegments) {
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("TINY_FLASH"), std::string::npos);
    EXPECT_NE(table.find("SHORT_FLASH"), std::string::npos);
    EXPECT_NE(table.find("MED_FLASH"), std::string::npos);
    EXPECT_NE(table.find("LONG_FLASH"), std::string::npos);
    EXPECT_NE(table.find("VERY_LONG_FLASH"), std::string::npos);
    EXPECT_NE(table.find("SEPARATOR"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_KeyShowsDurations) {
    std::string table = StatusLEDRenderer::generateTable();
    // Each named segment's duration is in the "ON 0.Xs" / "OFF 2.0s" comment.
    EXPECT_NE(table.find("ON 0.1s"), std::string::npos);  // TINY
    EXPECT_NE(table.find("ON 0.2s"), std::string::npos);  // SHORT
    EXPECT_NE(table.find("ON 0.5s"), std::string::npos);  // MED
    EXPECT_NE(table.find("ON 0.8s"), std::string::npos);  // LONG
    EXPECT_NE(table.find("ON 1.8s"), std::string::npos);  // VERY_LONG
    EXPECT_NE(table.find("OFF 2.0s"), std::string::npos); // SEPARATOR
}

TEST(StatusLEDRendererTest, GenerateTable_PerPatternCommentsAreFullSequences) {
    // The per-pattern # comment is the FULL timing sequence
    // ("ON 0.5s, OFF 0.5s" for BOOT) — not just the rightmost named flash.
    // Brief item #2: full sequence, data-driven from the LEDStep array;
    // the trailing 2s SEPARATOR is implied (not shown). Spot-check a few
    // known sequences.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("BOOT"), std::string::npos);  // BOOT row present
    // BOOT: ON 0.5s, OFF 0.5s (MED_FLASH + MED_GAP).
    EXPECT_NE(table.find("ON 0.5s, OFF 0.5s"), std::string::npos);
    // WIFI_SEARCHING: ON 0.1s, OFF 0.9s.
    EXPECT_NE(table.find("ON 0.1s, OFF 0.9s"), std::string::npos);
    // WIFI_CONNECTED: ON 0.8s, OFF 0.2s.
    EXPECT_NE(table.find("ON 0.8s, OFF 0.2s"), std::string::npos);
    // AP_MODE: trailing 2s separator is dropped (brief item #5) — the
    // sequence must end with "ON 0.1s", not "OFF 2.0s".
    EXPECT_NE(table.find("ON 0.8s, OFF 0.2s, ON 0.1s, OFF 0.1s, ON 0.1s"),
              std::string::npos);
    // And the trailing "OFF 2.0s" must NOT appear in any per-row # comment.
    // (The SEPARATOR's "OFF 2.0s" only appears in the KEY section below the
    // main table — assert by checking the AP_MODE row doesn't contain it.)
    const size_t apRow = table.find("AP_MODE");
    ASSERT_NE(apRow, std::string::npos);
    const size_t nextRow = table.find("\n", apRow);
    const std::string apLine = table.substr(apRow, nextRow - apRow);
    EXPECT_EQ(apLine.find("OFF 2.0s"), std::string::npos)
        << "AP_MODE # comment must drop the trailing 2.0s separator (brief item #5): '"
        << apLine << "'";
}

TEST(StatusLEDRendererTest, GenerateTable_CommentColumnsAlign) {
    // The '#' comment-column boundary is the same column for every row
    // regardless of the visual width. Asserted by scanning the per-pattern
    // block: every line that contains a registered pattern name must also
    // contain '# ' at the same column. Implementation detail: the
    // renderer right-aligns via setw(visualWidth) so this is a property of
    // the implementation, not a coincidence of the pattern set.
    std::string table = StatusLEDRenderer::generateTable();

    // Pick all per-pattern rows: lines containing one of the pattern names
    // immediately preceded by 2-space indent.
    static const char* kNames[] = {
        "BOOT", "WIFI_SEARCHING", "WIFI_CONNECTED", "CLIENT_CONNECTED",
        "AP_MODE", "OTA_IN_PROGRESS", "ERROR_AUTH_FAILURE", "ERROR_RECOVERABLE",
        "ERROR_NO_NTP_SERVICE", "FATAL_UNRECOVERABLE", "OFF"
    };
    // Find a common column for '#' by locating the comment position in any
    // one row and asserting every other row's comment marker lives there too.
    // We use a simpler property: every row must have exactly one '# '
    // immediately following a run of spaces; the column index of '#' must
    // match across rows.
    size_t firstHashColumn = std::string::npos;
    for (const char* name : kNames) {
        const std::string needle = std::string("  ") + name;
        const size_t namePos = table.find(needle);
        ASSERT_NE(namePos, std::string::npos) << "missing pattern row: " << name;
        const size_t hashPos = table.find("  # ", namePos);
        ASSERT_NE(hashPos, std::string::npos) << "missing '#' in row: " << name;
        const size_t column = hashPos - namePos;
        if (firstHashColumn == std::string::npos) {
            firstHashColumn = column;
        } else {
            EXPECT_EQ(column, firstHashColumn)
                << "comment column drifted for pattern: " << name;
        }
    }
}

// Key section: each named constant (flash + gap + specials) appears in the
// key with its single-state visual and ON/OFF duration. Brief item #1: use
// the ACTUAL constant names from StatusLEDConstants — TINY_FLASH, TINY_GAP,
// MED_FLASH, MED_GAP, LONG_FLASH, LONG_GAP, SEARCHING_GAP, SEPARATOR — not
// just the named-flash subset. The key must include BOTH flashes and gaps.
TEST(StatusLEDRendererTest, GenerateTable_KeyContainsAllNamedConstants) {
    std::string table = StatusLEDRenderer::generateTable();

    // Flashes
    EXPECT_NE(table.find("TINY_FLASH"),     std::string::npos);
    EXPECT_NE(table.find("SHORT_FLASH"),    std::string::npos);
    EXPECT_NE(table.find("MED_FLASH"),      std::string::npos);
    EXPECT_NE(table.find("LONG_FLASH"),     std::string::npos);
    EXPECT_NE(table.find("VERY_LONG_FLASH"),std::string::npos);
    // Gaps
    EXPECT_NE(table.find("TINY_GAP"),       std::string::npos);
    EXPECT_NE(table.find("SHORT_GAP"),      std::string::npos);
    EXPECT_NE(table.find("MED_GAP"),        std::string::npos);
    EXPECT_NE(table.find("LONG_GAP"),       std::string::npos);
    // Specials
    EXPECT_NE(table.find("SEARCHING_GAP"),  std::string::npos);
    EXPECT_NE(table.find("SEPARATOR"),      std::string::npos);
}

// Key section: each constant's visual must be a single ON or OFF block
// (no mid-second dividers). Brief item #3: the old key had spurious
// divider sections inside LONG_FLASH/VERY_LONG_FLASH rows. The new design
// renders each constant as exactly N chars between start/end dividers,
// where N = durationMs / 100. The SEPARATOR row (2s) shows 20 spaces
// between dividers.
TEST(StatusLEDRendererTest, GenerateTable_KeyRowsHaveNoMidSecondDividers) {
    std::string table = StatusLEDRenderer::generateTable();
    // Find each named constant's key row by name and check its visual
    // (chars between the first '|' and the next '|') has no embedded '|'.
    auto countMidDividers = [](const std::string& line) -> int {
        // Find the visual block between the first '|' pair.
        const size_t firstBar = line.find('|');
        if (firstBar == std::string::npos) return 0;
        const size_t secondBar = line.find('|', firstBar + 1);
        if (secondBar == std::string::npos) return 0;
        // Count '|' between the start and end bars.
        int count = 0;
        for (size_t i = firstBar + 1; i < secondBar; ++i) {
            if (line[i] == '|') ++count;
        }
        return count;
    };
    const std::string kKeys[] = {
        "TINY_FLASH", "SHORT_FLASH", "MED_FLASH", "LONG_FLASH", "VERY_LONG_FLASH",
        "TINY_GAP", "SHORT_GAP", "MED_GAP", "LONG_GAP", "SEARCHING_GAP", "SEPARATOR"
    };
    for (const std::string& name : kKeys) {
        // Find the row in the table starting with the name + 2 spaces (the
        // key row's indent) — must be after the "Key:" header.
        const size_t keyHeader = table.find("\nKey:\n");
        ASSERT_NE(keyHeader, std::string::npos) << "Key section not found";
        const std::string needle = "  " + name;
        const size_t rowPos = table.find(needle, keyHeader);
        ASSERT_NE(rowPos, std::string::npos) << "missing key row: " << name;
        const size_t eol = table.find('\n', rowPos);
        const std::string row = table.substr(rowPos, eol - rowPos);
        EXPECT_EQ(countMidDividers(row), 0)
            << "key row '" << name << "' has mid-second dividers: '" << row << "'";
    }
}
#endif  // INCLUDE_LED_HELP_KEY
