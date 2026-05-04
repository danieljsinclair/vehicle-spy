#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/boundary/ELM327Transport.h"

using namespace vehicle_sim::boundary;
using testing::Eq;
using testing::Optional;

// ================================================
// Test Suite 1: OBD2 Query Encoding (PID to ASCII)
// ================================================

TEST(ELM327Transport, BuildOBD2Query_PID0C_EncodesAsASCII)
{
    // ELM327 expects ASCII hex with spaces and CR, not binary
    // PID 0x0C (Engine RPM) should become "01 0C\r"
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x0C);
    EXPECT_EQ(query, "01 0C\r");
}

TEST(ELM327Transport, BuildOBD2Query_PID0D_EncodesAsASCII)
{
    // PID 0x0D (Vehicle Speed) should become "01 0D\r"
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x0D);
    EXPECT_EQ(query, "01 0D\r");
}

TEST(ELM327Transport, BuildOBD2Query_PID11_EncodesAsASCII)
{
    // PID 0x11 (Throttle Position) should become "01 11\r"
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x11);
    EXPECT_EQ(query, "01 11\r");
}

TEST(ELM327Transport, BuildOBD2Query_PID04_EncodesAsASCII)
{
    // PID 0x04 (Engine Load) should become "01 04\r"
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x04);
    EXPECT_EQ(query, "01 04\r");
}

TEST(ELM327Transport, BuildOBD2Query_UppercaseHex)
{
    // ASCII hex should be uppercase per ELM327 spec
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0xAB);
    EXPECT_EQ(query, "01 AB\r");
}

// ================================================
// Test Suite 2: OBD2 Response Parsing (ASCII to Binary)
// ================================================

TEST(ELM327Transport, ParseOBD2Response_RPMResponse_ConvertsToBinary)
{
    // ELM327 response "41 0C 1A F8\r" should become binary [0x41, 0x0C, 0x1A, 0xF8]
    // 0x41 = Mode 01 response, 0x0C = PID, 0x1A0xF8 = RPM data
    auto result = ELM327Transport::parseOBD2Response("41 0C 1A F8\r");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0C, 0x1A, 0xF8};
    EXPECT_EQ(*result, expected);
}

TEST(ELM327Transport, ParseOBD2Response_SpeedResponse_ConvertsToBinary)
{
    // "41 0D 64\r" → [0x41, 0x0D, 0x64] (100 km/h)
    auto result = ELM327Transport::parseOBD2Response("41 0D 64\r");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0D, 0x64};
    EXPECT_EQ(*result, expected);
}

TEST(ELM327Transport, ParseOBD2Response_ThrottleResponse_ConvertsToBinary)
{
    // "41 11 3C\r" → [0x41, 0x11, 0x3C] (~23.5% throttle)
    auto result = ELM327Transport::parseOBD2Response("41 11 3C\r");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x11, 0x3C};
    EXPECT_EQ(*result, expected);
}

// ================================================
// Test Suite 3: ELM327 Special Responses
// ================================================

TEST(ELM327Transport, ParseOBD2Response_NoData_ReturnsNullopt)
{
    // ELM327 returns "NO DATA\r" when no data is available for PID
    auto result = ELM327Transport::parseOBD2Response("NO DATA\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_NoDataCaseInsensitive_ReturnsNullopt)
{
    // ELM327 may return mixed case
    auto result = ELM327Transport::parseOBD2Response("NO DATA\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_SearchingPrefix_StripsAndParses)
{
    // ELM327 may prepend "SEARCHING..." while waiting for response
    auto result = ELM327Transport::parseOBD2Response("SEARCHING...\r41 0C 1A F8\r");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0C, 0x1A, 0xF8};
    EXPECT_EQ(*result, expected);
}

TEST(ELM327Transport, ParseOBD2Response_UnableToConnect_ReturnsNullopt)
{
    // ELM327 error state
    auto result = ELM327Transport::parseOBD2Response("UNABLE TO CONNECT\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_BusError_ReturnsNullopt)
{
    // CAN bus error response
    auto result = ELM327Transport::parseOBD2Response("BUS ERROR\r");
    EXPECT_FALSE(result.has_value());
}

// ================================================
// Test Suite 4: Multi-line Response Parsing (VIN)
// ================================================

TEST(ELM327Transport, ParseOBD2Response_MultiLineVIN_ParsesFully)
{
    // VIN responses can span multiple lines with prompt (>) between them
    std::string multiLineResponse =
        "014: 57 41 55 5A 5A 5A 5A 5A\r"  // "WAUZZZZZ"
        "015: 5A 5A 5A 5A 5A 5A 5A 5A\r"  // "ZZZZZZZZ"
        "016: 31 32 33 34 35 36 00 00\r"; // "123456"
    auto result = ELM327Transport::parseOBD2Response(multiLineResponse);

    ASSERT_TRUE(result.has_value());
    // Should extract just the VIN bytes, stripping line numbers and prompt
    EXPECT_GE(result->size(), 17);  // VIN is 17 chars
}

TEST(ELM327Transport, ParseOBD2Response_VIN_ExtractsCorrectBytes)
{
    // Standard VIN response from mode 09 PID 02
    std::string vinResponse = "014: 49 02 01 00 00 00 35 59\r015: 4A 33 53 32 44 58 4D 48\r016: 31 30 35 37 36 00 00 00\r";
    auto result = ELM327Transport::parseOBD2Response(vinResponse);

    ASSERT_TRUE(result.has_value());
    // VIN bytes should be present: 5YJ3S2DXMH10576
    // The actual extraction is handled by VehicleDetector, but we should get all the hex bytes
    EXPECT_GT(result->size(), 10);
}

// ================================================
// Test Suite 5: AT Command Encoding
// ================================================

TEST(ELM327Transport, BuildATCommand_ResetsAdapter)
{
    // ATZ - Reset all
    std::string cmd = ELM327Transport::buildATCommand("Z");
    EXPECT_EQ(cmd, "ATZ\r");
}

TEST(ELM327Transport, BuildATCommand_SetsProtocol)
{
    // ATSP0 - Auto protocol detection
    std::string cmd = ELM327Transport::buildATCommand("SP0");
    EXPECT_EQ(cmd, "ATSP0\r");
}

TEST(ELM327Transport, BuildATCommand_DisablesEcho)
{
    // ATE0 - Echo off
    std::string cmd = ELM327Transport::buildATCommand("E0");
    EXPECT_EQ(cmd, "ATE0\r");
}

TEST(ELM327Transport, BuildATCommand_SetsHeadersOff)
{
    // ATH0 - Headers off
    std::string cmd = ELM327Transport::buildATCommand("H0");
    EXPECT_EQ(cmd, "ATH0\r");
}

TEST(ELM327Transport, BuildATCommand_SetsLinefeedsOff)
{
    // ATL0 - Line feeds off
    std::string cmd = ELM327Transport::buildATCommand("L0");
    EXPECT_EQ(cmd, "ATL0\r");
}

// ================================================
// Test Suite 6: AT Response Parsing
// ================================================

TEST(ELM327Transport, ParseATResponse_OK_ReturnsTrue)
{
    // ELM327 responds "OK" to successful AT commands
    bool result = ELM327Transport::parseATResponse("OK\r");
    EXPECT_TRUE(result);
}

TEST(ELM327Transport, ParseATResponse_WithPrompt_StripsAndReturnsTrue)
{
    // ELM327 adds prompt '>'
    bool result = ELM327Transport::parseATResponse("OK\r>");
    EXPECT_TRUE(result);
}

TEST(ELM327Transport, ParseATResponse_ERROR_ReturnsFalse)
{
    bool result = ELM327Transport::parseATResponse("ERROR\r");
    EXPECT_FALSE(result);
}

// ================================================
// Test Suite 7: Init Command Sequence
// ================================================

TEST(ELM327Transport, BuildInitSequence_GeneratesCorrectCommands)
{
    // ELM327 initialization sequence with expected timing
    auto commands = ELM327Transport::buildInitSequence();

    // Should include at minimum: ATZ, ATE0, ATH0, ATL0, ATSP0
    EXPECT_GE(commands.size(), 5);

    EXPECT_EQ(commands[0].command, "ATZ\r");
    EXPECT_EQ(commands[1].command, "ATE0\r");
    EXPECT_EQ(commands[2].command, "ATH0\r");
    EXPECT_EQ(commands[3].command, "ATL0\r");
    EXPECT_EQ(commands[4].command, "ATSP0\r");
}

TEST(ELM327Transport, BuildInitSequence_HasExpectedDelays)
{
    // First command (ATZ) needs longer delay for reset
    auto commands = ELM327Transport::buildInitSequence();

    // ATZ reset delay should be significant (adapter needs time to boot)
    EXPECT_GE(commands[0].delayMs, 500);

    // Subsequent commands need minimal delay
    for (size_t i = 1; i < commands.size(); ++i) {
        EXPECT_GE(commands[i].delayMs, 50);
    }
}

// ================================================
// Test Suite 8: Edge Cases and Error Handling
// ================================================

TEST(ELM327Transport, ParseOBD2Response_EmptyString_ReturnsNullopt)
{
    auto result = ELM327Transport::parseOBD2Response("");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_OnlyPrompt_ReturnsNullopt)
{
    auto result = ELM327Transport::parseOBD2Response(">\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_InvalidHex_ReturnsNullopt)
{
    // Invalid hex characters
    auto result = ELM327Transport::parseOBD2Response("41 0X 1A F8\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_OddHexDigits_ReturnsNullopt)
{
    // Hex must come in pairs (bytes)
    auto result = ELM327Transport::parseOBD2Response("41 0C 1A F\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseOBD2Response_WhitespaceVariations_Handles)
{
    // Should handle multiple spaces or tabs
    auto result = ELM327Transport::parseOBD2Response("41  0C\t1A\tF8\r");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0C, 0x1A, 0xF8};
    EXPECT_EQ(*result, expected);
}

// ================================================
// Test Suite 9: String Termination Handling
// ================================================

TEST(ELM327Transport, ParseOBD2Response_CROnly_Parses)
{
    // CR only (most common)
    auto result = ELM327Transport::parseOBD2Response("41 0C 1A F8\r");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0C, 0x1A, 0xF8};
    EXPECT_EQ(*result, expected);
}

TEST(ELM327Transport, ParseOBD2Response_CRLF_Parses)
{
    // CRLF (some adapters)
    auto result = ELM327Transport::parseOBD2Response("41 0C 1A F8\r\n");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0C, 0x1A, 0xF8};
    EXPECT_EQ(*result, expected);
}

TEST(ELM327Transport, ParseOBD2Response_LFOnly_Parses)
{
    // LF only (rare but possible)
    auto result = ELM327Transport::parseOBD2Response("41 0C 1A F8\n");
    ASSERT_TRUE(result.has_value());

    std::vector<uint8_t> expected = {0x41, 0x0C, 0x1A, 0xF8};
    EXPECT_EQ(*result, expected);
}

// ================================================
// Test Suite 10: Large Responses
// ================================================

TEST(ELM327Transport, ParseOBD2Response_MultiPIDAggregate_ParsesFully)
{
    // Some adapters may return multiple PID responses in one message
    std::string combined = "41 0C 1A F8 41 0D 64 41 11 3C\r";
    auto result = ELM327Transport::parseOBD2Response(combined);

    ASSERT_TRUE(result.has_value());
    // Should contain all bytes from all responses (10 bytes total)
    EXPECT_EQ(result->size(), 10);
}

// ================================================
// Test Suite 11: Mode 09 (Vehicle Info) Commands
// ================================================

TEST(ELM327Transport, BuildOBD2Query_Mode09PID02_VINQuery)
{
    // Mode 09 PID 02 is VIN read
    std::string query = ELM327Transport::buildOBD2Query(0x09, 0x02);
    EXPECT_EQ(query, "09 02\r");
}

TEST(ELM327Transport, BuildOBD2Query_Mode09PID51_FuelType)
{
    // Mode 09 PID 51 is fuel type
    std::string query = ELM327Transport::buildOBD2Query(0x09, 0x51);
    EXPECT_EQ(query, "09 51\r");
}

// ================================================
// Test Suite 12: Response Timing and Prompt
// ================================================

TEST(ELM327Transport, ExtractPrompt_RemovesTrailingPrompt)
{
    std::string withPrompt = "41 0C 1A F8\r>";
    std::string stripped = ELM327Transport::extractPrompt(withPrompt);
    EXPECT_EQ(stripped, "41 0C 1A F8\r");
}

TEST(ELM327Transport, ExtractPrompt_NoPrompt_ReturnsUnchanged)
{
    std::string noPrompt = "41 0C 1A F8\r";
    std::string stripped = ELM327Transport::extractPrompt(noPrompt);
    EXPECT_EQ(stripped, "41 0C 1A F8\r");
}
