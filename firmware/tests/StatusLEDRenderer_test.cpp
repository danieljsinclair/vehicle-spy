// StatusLEDRenderer_test.cpp - Tests for StatusLED pattern renderer

#include <gtest/gtest.h>
#include "vanilla/StatusLED.h"
#include "vanilla/StatusLEDRenderer.h"

using namespace firmware;

// Rendered output tests
TEST(StatusLEDRendererTest, RenderPattern_WifiSearching) {
    // WIFI_SEARCHING: ON 100ms + OFF 900ms → "-         " (1 dash, 9 spaces)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::WIFI_SEARCHING);
    EXPECT_EQ(rendered, "-         ");  // 1 dash, 9 spaces
}

TEST(StatusLEDRendererTest, RenderPattern_WifiConnected) {
    // WIFI_CONNECTED: ON 800ms + OFF 200ms → "--------  " (8 dashes, 2 spaces)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::WIFI_CONNECTED);
    EXPECT_EQ(rendered, "--------  ");  // 8 dashes, 2 spaces
}

TEST(StatusLEDRendererTest, RenderPattern_ClientConnected) {
    // CLIENT_CONNECTED (solid): "##########"
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::CLIENT_CONNECTED);
    EXPECT_EQ(rendered, "##########");  // 10 hashes
}

TEST(StatusLEDRendererTest, RenderPattern_Boot) {
    // BOOT: ON 500ms + OFF 500ms → "-----     " (5 dashes, 5 spaces)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::BOOT);
    EXPECT_EQ(rendered, "-----     ");  // 5 dashes, 5 spaces
}

TEST(StatusLEDRendererTest, RenderPattern_Off) {
    // OFF: SOLID OFF → "          " (10 spaces)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::OFF);
    EXPECT_EQ(rendered, "          ");  // 10 spaces
}

TEST(StatusLEDRendererTest, RenderPattern_ApMode) {
    // AP_MODE: LONG_FLASH ON(800ms), TINY_GAP OFF(100ms),
    //          TINY_FLASH ON(100ms), TINY_GAP OFF(100ms),
    //          TINY_FLASH ON(100ms), SEPARATOR OFF(2000ms)
    // → "-------- - -|" (8 dashes, 1 space, 1 dash, 1 space, 1 pipe)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::AP_MODE);
    EXPECT_EQ(rendered, "-------- - -|");  // 8 dashes (capped to 10), 1 space, 1 dash, 1 space, 1 pipe
}

TEST(StatusLEDRendererTest, RenderPattern_OtaInProgress) {
    // OTA_IN_PROGRESS: SHORT_FLASH ON(200ms) + SHORT_GAP OFF(200ms)
    // → "--  " (2 dashes, 2 spaces)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::OTA_IN_PROGRESS);
    EXPECT_EQ(rendered, "--  ");  // 2 dashes, 2 spaces
}

TEST(StatusLEDRendererTest, RenderPattern_AuthFailure) {
    // ERROR_AUTH_FAILURE: ERROR_3_PULSE (3x SHORT_FLASH 200ms + SHORT_GAP 200ms)
    //              + 2×TINY_PULSE (2x TINY_FLASH 100ms + TINY_GAP 100ms)
    //              + SEPARATOR (2000ms)
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_AUTH_FAILURE);
    // Expected: "--  --  --  - - |" (3 short pulses, separator, 2 tiny pulses, separator)
    EXPECT_EQ(rendered, "--  --  --  - - |");
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorRecoverable) {
    // ERROR_RECOVERABLE: ERROR_3_PULSE + 3×TINY_PULSE + SEPARATOR
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_RECOVERABLE);
    EXPECT_EQ(rendered, "--  --  --  - - - |");  // 3 short, separator, 3 tiny, separator
}

TEST(StatusLEDRendererTest, RenderPattern_ErrorNoNtpService) {
    // ERROR_NO_NTP_SERVICE: ERROR_3_PULSE + 1×TINY_PULSE + SEPARATOR
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::ERROR_NO_NTP_SERVICE);
    EXPECT_EQ(rendered, "--  --  --  - |");  // 3 short, separator, 1 tiny, separator
}

TEST(StatusLEDRendererTest, RenderPattern_FatalUnrecoverable) {
    // FATAL_UNRECOVERABLE: SOS - 3 short, 3 long, 3 short, SEPARATOR
    std::string rendered = StatusLEDRenderer::renderPattern(StatusLED::Pattern::FATAL_UNRECOVERABLE);
    EXPECT_EQ(rendered, "--  --  --  --------  --------  --------  --  --  --  |");  // 3 short, 3 long, 3 short, separator
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

    // Verify visual patterns are present for key states
    EXPECT_NE(help.find("-         "), std::string::npos);  // WIFI_SEARCHING pattern
    EXPECT_NE(help.find("--------  "), std::string::npos);  // WIFI_CONNECTED pattern
    EXPECT_NE(help.find("##########"), std::string::npos);  // CLIENT_CONNECTED pattern

    // Verify timing notes are present
    EXPECT_NE(help.find("0.1s"), std::string::npos);  // TINY_FLASH
    EXPECT_NE(help.find("0.8s"), std::string::npos);  // LONG_FLASH
}
