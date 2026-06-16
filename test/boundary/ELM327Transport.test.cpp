#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/boundary/ELM327Transport.h"

using namespace vehicle_sim::boundary;
using testing::Eq;
using testing::Optional;

// ================================================
// Test Suite 1: CAN Frame Parsing (Core Functionality)
// ================================================

TEST(ELM327Transport, ParseCANFrame_ValidFrameWithoutType_ParsesCorrectly)
{
    // Tesla CAN frame: "264 00 00 00 90 01 10 27 00"
    // 264 = CAN ID (hex), followed by 8 data bytes
    auto result = ELM327Transport::parseCANFrame("264 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->canId, 0x264);
    EXPECT_EQ(result->data.size(), 8);
    EXPECT_EQ(result->data[0], 0x00);
    EXPECT_EQ(result->data[1], 0x00);
    EXPECT_EQ(result->data[2], 0x00);
    EXPECT_EQ(result->data[3], 0x90);
    EXPECT_EQ(result->data[4], 0x01);
    EXPECT_EQ(result->data[5], 0x10);
    EXPECT_EQ(result->data[6], 0x27);
    EXPECT_EQ(result->data[7], 0x00);
}

TEST(ELM327Transport, ParseCANFrame_ValidFrameWithTypePrefix_ParsesCorrectly)
{
    // CAN frame with type byte: "610 264 00 00 00 90 01 10 27 00"
    // 610 = type byte (0x600 + type), 264 = CAN ID, then 8 data bytes
    auto result = ELM327Transport::parseCANFrame("610 264 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->canId, 0x264);
    EXPECT_EQ(result->data.size(), 8);
    EXPECT_EQ(result->data[0], 0x00);
    EXPECT_EQ(result->data[1], 0x00);
    EXPECT_EQ(result->data[2], 0x00);
    EXPECT_EQ(result->data[3], 0x90);
    EXPECT_EQ(result->data[4], 0x01);
    EXPECT_EQ(result->data[5], 0x10);
    EXPECT_EQ(result->data[6], 0x27);
    EXPECT_EQ(result->data[7], 0x00);
}

TEST(ELM327Transport, ParseCANFrame_Prompt_ReturnsNullopt)
{
    // ELM327 prompt character
    auto result = ELM327Transport::parseCANFrame(">");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_PromptWithCR_ReturnsNullopt)
{
    auto result = ELM327Transport::parseCANFrame(">\r");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_EmptyString_ReturnsNullopt)
{
    auto result = ELM327Transport::parseCANFrame("");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_OBD2Response_ReturnsNullopt)
{
    // Standard OBD2 response, not a CAN frame
    auto result = ELM327Transport::parseCANFrame("41 0C 1A F8");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_InvalidHex_ReturnsNullopt)
{
    // Invalid hex characters
    auto result = ELM327Transport::parseCANFrame("264 00 00 00 90 01 10 XX 00");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_WithLineNumbers_ReturnsNullopt)
{
    // Line numbers like "014:" indicate OBD2 multi-frame response
    auto result = ELM327Transport::parseCANFrame("014: 264 00 00 00 90 01 10 27");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_ErrorMessage_ReturnsNullopt)
{
    // ELM327 error messages
    auto result = ELM327Transport::parseCANFrame("NO DATA");
    EXPECT_FALSE(result.has_value());

    result = ELM327Transport::parseCANFrame("ERROR");
    EXPECT_FALSE(result.has_value());
}

// ================================================
// Test Suite 2: CAN Monitor Init Sequence
// ================================================

TEST(ELM327Transport, BuildCANMonitorInitSequence_GeneratesCorrectCommands)
{
    auto commands = ELM327Transport::buildCANMonitorInitSequence();

    // Should have 5 commands: ATZ, AT E0, AT SP 6, AT H 1, AT MA
    EXPECT_EQ(commands.size(), 5);

    EXPECT_EQ(commands[0].command, "ATZ\r");
    EXPECT_EQ(commands[1].command, "ATE0\r");
    EXPECT_EQ(commands[2].command, "ATSP6\r");
    EXPECT_EQ(commands[3].command, "ATH1\r");
    EXPECT_EQ(commands[4].command, "ATMA\r");
}

TEST(ELM327Transport, BuildCANMonitorInitSequence_HasExpectedDelays)
{
    auto commands = ELM327Transport::buildCANMonitorInitSequence();

    // ATZ reset needs longer delay
    EXPECT_GE(commands[0].delayMs, 500);

    // Subsequent commands need minimal delay
    for (size_t i = 1; i < commands.size(); ++i) {
        EXPECT_GE(commands[i].delayMs, 50);
    }
}

TEST(ELM327Transport, BuildCANMonitorInitSequence_TerminatesWithMonitorAll)
{
    // The prompt-driven sequence must end in ATMA (monitor-all): that is the
    // command that puts the adapter into the streaming monitor mode whose
    // "<ID> <D0>..<D7>" output the Elm327Normaliser consumes. This pins the
    // contract between the init sequencer (transport) and the line normaliser
    // without requiring live hardware.
    auto commands = ELM327Transport::buildCANMonitorInitSequence();
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.back().command, "ATMA\r");
}

// ================================================
// Test Suite 3: CAN Frame Filter (Optional Feature)
// ================================================

TEST(ELM327Transport, BuildCANFilter_ConstructsFilterCommand)
{
    // Filter for CAN ID 0x264
    std::string filter = ELM327Transport::buildCANFilter(0x264);
    EXPECT_EQ(filter, "ATCRA264\r");
}

TEST(ELM327Transport, BuildCANFilter_HexID_ConstructsFilterCommand)
{
    // Filter for CAN ID 0x102 (hex 102)
    std::string filter = ELM327Transport::buildCANFilter(0x102);
    EXPECT_EQ(filter, "ATCRA102\r");
}

TEST(ELM327Transport, BuildCANFilter_ZeroID_ConstructsFilterCommand)
{
    // Filter for CAN ID 0
    std::string filter = ELM327Transport::buildCANFilter(0);
    EXPECT_EQ(filter, "ATCRA0\r");
}

// ================================================
// Test Suite 4: CAN Frame Edge Cases
// ================================================

TEST(ELM327Transport, ParseCANFrame_WithCRLF_ParsesCorrectly)
{
    // CAN frame with CRLF termination
    auto result = ELM327Transport::parseCANFrame("264 00 00 00 90 01 10 27 00\r\n");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->canId, 0x264);
    EXPECT_EQ(result->data.size(), 8);
}

TEST(ELM327Transport, ParseCANFrame_WithSpaces_ParsesCorrectly)
{
    // Multiple spaces between bytes
    auto result = ELM327Transport::parseCANFrame("264  00  00  00  90  01  10  27  00");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->canId, 0x264);
    EXPECT_EQ(result->data.size(), 8);
}

TEST(ELM327Transport, ParseCANFrame_LowercaseHex_ParsesCorrectly)
{
    // Lowercase hex values
    auto result = ELM327Transport::parseCANFrame("264 aa bb cc dd ee ff 00 11");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->canId, 0x264);
    EXPECT_EQ(result->data[0], 0xAA);
    EXPECT_EQ(result->data[1], 0xBB);
    EXPECT_EQ(result->data[2], 0xCC);
    EXPECT_EQ(result->data[3], 0xDD);
    EXPECT_EQ(result->data[4], 0xEE);
    EXPECT_EQ(result->data[5], 0xFF);
}

TEST(ELM327Transport, ParseCANFrame_ExtendedCANID_ParsesCorrectly)
{
    // Extended CAN ID (29-bit) in 11-bit mode - should still parse the ID as-is
    auto result = ELM327Transport::parseCANFrame("1AA 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->canId, 0x1AA);
    EXPECT_EQ(result->data.size(), 8);
}

TEST(ELM327Transport, ParseCANFrame_WithOKMessage_ReturnsNullopt)
{
    // OK message from AT command
    auto result = ELM327Transport::parseCANFrame("OK");
    EXPECT_FALSE(result.has_value());
}

TEST(ELM327Transport, ParseCANFrame_WithBusInit_ReturnsNullopt)
{
    // BUS INIT message
    auto result = ELM327Transport::parseCANFrame("BUS INIT");
    EXPECT_FALSE(result.has_value());
}
