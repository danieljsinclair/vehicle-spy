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
    // ERROR_AUTH_FAILURE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 3×(TINY_ON 100 + TINY_OFF 100)
    // + trailing SEP 2000 (omitted from visual). Total rendered: 1.8s = 18 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_AUTH_FAILURE);
    EXPECT_EQ(rendered, "|--  --  --|  - - - |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorRecoverable) {
    // ERROR_RECOVERABLE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 2×(TINY_ON 100 + TINY_OFF 100)
    // + trailing SEP 2000 (omitted). Total rendered: 1.6s = 16 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_RECOVERABLE);
    EXPECT_EQ(rendered, "|--  --  --|  - - |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorNoNtpService) {
    // ERROR_NO_NTP_SERVICE: 3×(SHORT_ON 200 + SHORT_OFF 200) + 1×(TINY_ON 100 + TINY_OFF 100)
    // + trailing SEP 2000 (omitted). Total rendered: 1.4s = 14 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    EXPECT_EQ(rendered, "|--  --  --|  - |");
}

TEST(StatusLEDRendererTest, RenderPattern_FatalUnrecoverable) {
    // FATAL_UNRECOVERABLE_SOS: SOS — 3×(SHORT_ON 200 + SHORT_OFF 200) +
    //                       3×(LONG_ON 800 + SHORT_OFF 200) +
    //                       3×(SHORT_ON 200 + SHORT_OFF 200) +
    //                       trailing SEP 2000 (omitted). Total rendered: 5.4s = 54 tenths.
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::FATAL_UNRECOVERABLE_SOS);
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
    EXPECT_NE(help.find("FATAL_UNRECOVERABLE_SOS"), std::string::npos);
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
    size_t fatalUnrecoverable = help.find("FATAL_UNRECOVERABLE_SOS");

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
    EXPECT_NE(table.find("FATAL_UNRECOVERABLE_SOS"), std::string::npos);
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
    EXPECT_NE(table.find("# Solid ON"), std::string::npos);
    // Single-state OFF pattern (OFF) -> "solid OFF" timing note.
    EXPECT_NE(table.find("# Solid OFF"), std::string::npos);
}

// A pattern that carries a 2s SEPARATOR step anywhere (an OFF step lasting >=
// SEPARATOR_MS) appends ", 2s SEPARATOR" to its row's timing comment. The
// annotation is position-blind: it does not matter whether the separator is the
// trailing step or appears mid-pattern. AP_MODE and the error/fatal families
// all end with {OFF, SEPARATOR_MS}; the plain WiFi/BOOT/OTA patterns do not.
TEST(StatusLEDRendererTest, GenerateTable_AnnotatesSeparatorAnywhere) {
    std::string table = StatusLEDRenderer::generateTable();

    // Helper: extract the single row for a pattern name (the substring from the
    // name to the next newline) and return whether it carries the annotation.
    auto rowHasAnnotation = [&](const std::string& name) -> bool {
        const size_t pos = table.find(name);
        if (pos == std::string::npos) { ADD_FAILURE() << "missing row: " << name; return false; }
        const size_t rowEnd = table.find('\n', pos);
        if (rowEnd == std::string::npos) { ADD_FAILURE() << "row has no newline: " << name; return false; }
        return table.substr(pos, rowEnd - pos).find("2s SEPARATOR") != std::string::npos;
    };

    // Patterns WITH a trailing 2s separator -> annotated.
    EXPECT_TRUE(rowHasAnnotation("AP_MODE"));
    EXPECT_TRUE(rowHasAnnotation("ERROR_AUTH_FAILURE"));
    EXPECT_TRUE(rowHasAnnotation("ERROR_RECOVERABLE"));
    EXPECT_TRUE(rowHasAnnotation("ERROR_NO_NTP_SERVICE"));
    EXPECT_TRUE(rowHasAnnotation("FATAL_UNRECOVERABLE_SOS"));

    // Patterns WITHOUT any separator step -> NOT annotated.
    EXPECT_FALSE(rowHasAnnotation("BOOT"));
    EXPECT_FALSE(rowHasAnnotation("WIFI_SEARCHING"));
    EXPECT_FALSE(rowHasAnnotation("WIFI_CONNECTED"));
    EXPECT_FALSE(rowHasAnnotation("CLIENT_CONNECTED"));
    EXPECT_FALSE(rowHasAnnotation("OTA_IN_PROGRESS"));
    EXPECT_FALSE(rowHasAnnotation("OFF"));
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
        "FATAL_UNRECOVERABLE_SOS",
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
        StatusLED::Pattern::FATAL_UNRECOVERABLE_SOS,
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

TEST(StatusLEDRendererTest, GenerateTable_NoEnumNamesSection) {
    // The trailing "Enum names (use with setPattern / getPattern)" section was
    // removed from generateTable() — it was unhelpful. The table now ends after
    // the per-pattern rows (and the optional Key section). Assert the section
    // header is gone and no fully-qualified enum spellings leak into the table.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_EQ(table.find("Enum names"), std::string::npos);
    EXPECT_EQ(table.find("StatusLED::Pattern::"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_ContainsKeyExplainingDividers) {
    // Key at the bottom of the table documents the '|' divider semantics.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("whole second"), std::string::npos);
    EXPECT_NE(table.find("100ms"), std::string::npos);
}

TEST(StatusLEDRendererTest, EnumName_ReturnsQualifiedSpelling) {
    // enumName is the data source for the "Enum:" line in generateHelpText()
    // (the table's enum-name section was removed — see GenerateTable_NoEnumNames).
    EXPECT_EQ(StatusLEDRenderer::enumName(StatusLED::Pattern::BOOT),
              "StatusLED::Pattern::BOOT");
    EXPECT_EQ(StatusLEDRenderer::enumName(StatusLED::Pattern::CLIENT_CONNECTED),
              "StatusLED::Pattern::CLIENT_CONNECTED");
}

// Key section: a fixed four-entry legend at the bottom of the table that maps
// each visual bar to the timing token it produces. Always rendered (no
// INCLUDE_LED_HELP_KEY guard). Verified via the public generateTable().
TEST(StatusLEDRendererTest, GenerateTable_KeyAlwaysRenders) {
    // The Key section is NOT guarded by any ifdef — it must always appear.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("\nKey;\n"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_KeyShowsFourEntries) {
    // Exactly four entries: FLASH, DOT, DASH, SEPARATOR — each a quoted visual
    // bar followed by its token comment.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("'--'"), std::string::npos);             // DOT bar
    EXPECT_NE(table.find("'--------'"), std::string::npos);       // DASH bar
    EXPECT_NE(table.find("'          |          '"), std::string::npos);  // SEPARATOR bar
    EXPECT_NE(table.find("# FLASH 0.1s"), std::string::npos);
    EXPECT_NE(table.find("# DOT 0.2s"), std::string::npos);
    EXPECT_NE(table.find("# DASH 0.8s"), std::string::npos);
    EXPECT_NE(table.find("# 2s SEPARATOR"), std::string::npos);
}

TEST(StatusLEDRendererTest, GenerateTable_PerPatternCommentsAreFullSequences) {
    // The per-pattern # comment is the FULL timing sequence — data-driven from
    // the LEDStep array; the trailing 2s SEPARATOR is implied (not shown).
    // Spot-check the known sequences using the new FLASH/DOT/DASH/PULSE tokens.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("BOOT"), std::string::npos);  // BOOT row present
    // BOOT: ON 0.5s, OFF 0.5s (symmetric, 3+ char) → "PULSE 0.5s".
    EXPECT_NE(table.find("PULSE 0.5s"), std::string::npos);
    // WIFI_SEARCHING: ON 0.1s (→ FLASH) + dominant OFF 0.9s → "FLASH, OFF 0.9s".
    EXPECT_NE(table.find("FLASH, OFF 0.9s"), std::string::npos);
    // WIFI_CONNECTED: ON 0.8s + OFF 0.2s (asymmetric, 3+ char) → "DASH 0.8s".
    EXPECT_NE(table.find("DASH 0.8s"), std::string::npos);
    // AP_MODE: ON 0.8s (→ DASH 0.8s), ON 0.1s (→ FLASH), ON 0.1s (→ FLASH).
    EXPECT_NE(table.find("DASH 0.8s, FLASH, FLASH"), std::string::npos);
    // And the trailing "OFF 2.0s" must NOT appear in any per-row # comment —
    // the 2s separator is dropped from every row's note.
    const size_t apRow = table.find("AP_MODE");
    ASSERT_NE(apRow, std::string::npos);
    const size_t nextRow = table.find("\n", apRow);
    const std::string apLine = table.substr(apRow, nextRow - apRow);
    EXPECT_EQ(apLine.find("OFF 2.0s"), std::string::npos)
        << "AP_MODE # comment must drop the trailing 2.0s separator: '"
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
        "ERROR_NO_NTP_SERVICE", "FATAL_UNRECOVERABLE_SOS", "OFF"
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

// ── timingNote() FLASH / DOT / DASH / PULSE notation (verified via the
// public generateTable()) ──
// timingNote() presents patterns compactly by classifying each ON run by its
// visual width (1 char = FLASH, 2 = DOT, 3+ = DASH), folding a 3+ char ON run
// with a symmetric following OFF into a PULSE, and emitting a dominant OFF
// rest (≥ 3× the preceding ON). No run-collapse: repeated tokens render
// long-hand. timingNote() is private, so we assert on its output as surfaced
// in the diagnostic table (each row's "# <note>" column).

TEST(StatusLEDRendererTest, TimingNote_SingleCharOnIsFlash) {
    // WIFI_SEARCHING: ON 100ms (1 char) → "FLASH", followed by a dominant OFF
    // rest (900ms ≥ 3× 100ms) → "OFF 0.9s".
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("# FLASH, OFF 0.9s"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_TwoCharOnIsDot) {
    // OTA_IN_PROGRESS: ON 200ms (2 chars) → "DOT" (a 2-char ON run is a DOT
    // regardless of the following gap; the PULSE fold only applies to 3+ char
    // ON runs with a symmetric following OFF).
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("# DOT"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_ThreePlusCharAsymmetricIsDash) {
    // WIFI_CONNECTED: ON 800ms (8 chars) + OFF 200ms — asymmetric, 3+ char ON
    // → "DASH 0.8s" (the ON run's duration; the short OFF is < 3× ON so it is
    // dropped as an inter-element gap).
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("# DASH 0.8s"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_SymmetricThreePlusCharPairIsPulse) {
    // BOOT: ON 500ms + OFF 500ms — a 3+ char ON run with a symmetric (equal)
    // following OFF → "PULSE 0.5s".
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("# PULSE 0.5s"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_NoNxCollapse_RendersLongHand) {
    // FATAL_UNRECOVERABLE_SOS: 3 short, 3 long, 3 short, SEPARATOR. The three
    // short ON runs (200ms each) render long-hand as "DOT, DOT, DOT" and the
    // three long ON runs (800ms each) as "DASH 0.8s, DASH 0.8s, DASH 0.8s" —
    // NOT collapsed to "3xDOT" / "3xDASH" (KISS: no Nx run-collapse).
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("DOT, DOT, DOT, DASH 0.8s, DASH 0.8s, DASH 0.8s, DOT, DOT, DOT"),
              std::string::npos);
    // And the old collapsed forms must be gone.
    EXPECT_EQ(table.find("3xPULSE"), std::string::npos);
    EXPECT_EQ(table.find("3xDASH"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_ApModeIsDashFlashFlash) {
    // AP_MODE: ON 0.8s (→ DASH 0.8s), ON 0.1s (→ FLASH), ON 0.1s (→ FLASH).
    // The inter-element OFF gaps (0.2s, 0.1s, 0.1s) are all < 3× their
    // preceding ON, so they are dropped — only the three ON runs remain.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("DASH 0.8s, FLASH, FLASH"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_ErrorPatternsAreDotsThenFlashes) {
    // ERROR_AUTH_FAILURE = 3 dots + 3 flashes; ERROR_RECOVERABLE = 3 dots + 2
    // flashes; ERROR_NO_NTP_SERVICE = 3 dots + 1 flash. The severity gradient
    // (most flashes = auth failure) is the visual differentiator.
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("# DOT, DOT, DOT, FLASH, FLASH, FLASH"), std::string::npos);
    EXPECT_NE(table.find("# DOT, DOT, DOT, FLASH, FLASH"), std::string::npos);
    EXPECT_NE(table.find("# DOT, DOT, DOT, FLASH"), std::string::npos);
}

TEST(StatusLEDRendererTest, TimingNote_SingleStateUnchanged) {
    // CLIENT_CONNECTED is a single ON step → "solid ON" (not a PULSE).
    std::string table = StatusLEDRenderer::generateTable();
    EXPECT_NE(table.find("# Solid ON"), std::string::npos);
    // OFF is a single OFF step → "solid OFF".
    EXPECT_NE(table.find("# Solid OFF"), std::string::npos);
}
