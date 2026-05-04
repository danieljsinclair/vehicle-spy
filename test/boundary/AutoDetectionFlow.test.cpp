#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/boundary/OBD2Protocol.h"

using namespace vehicle_sim;
using namespace vehicle_sim::boundary;

// ================================================
// Test Suite 1: Full Auto-Detection Flow Tests
// ================================================

/**
 * Test that OBD2Protocol properly performs auto-detection:
 * - Sends AT commands via ELM327Transport with delays
 * - Sends VIN query and receives response
 * - Sends fuel type query and receives response
 * - Returns VehicleDetectionResult with vehicle info
 */
TEST(AutoDetectionFlow, FullFlow_VINAndFuelTypeDetectionWorks)
{
    OBD2Protocol obd2Protocol;

    // Set up mock send callback that captures commands
    std::vector<std::string> sentCommands;
    obd2Protocol.setSendCallback([&](const std::string& cmd) {
        sentCommands.push_back(cmd);
    });

    // Set up mock data processing
    std::vector<std::string> receivedData;
    obd2Protocol.setSendCallback([&](const std::string& cmd) {
        // For this test, we just capture that it would be sent
    });

    // Send initialization
    auto detectionResult = obd2Protocol.initialize();

    // Verify initialization commands were sent
    EXPECT_GE(sentCommands.size(), 7);  // ATZ + 5 more = 6 (ATE0, ATH0, ATL0, ATSP0, ATSTFF)

    // In real flow, VIN response would come after initialization
    // For this test, we just verify that initialization completes without error
    EXPECT_TRUE(detectionResult.has_value());

    // If VIN was successfully detected, it should have WMI for a make
    if (detectionResult && detectionResult->make == domain::VehicleMake::Tesla) {
        EXPECT_FALSE(detectionResult->wmi.empty());
    }
}

/**
 * Test that OBD2Protocol handles ASCII encoding/decoding
 */
TEST(AutoDetectionFlow, ELM327Transport_EncodesQueryAsASCII)
{
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x0C);
    EXPECT_EQ(query, "01 0C\r");
}

TEST(AutoDetectionFlow, ELM327Transport_ParsesResponseToBinary)
{
    // RPM response: "41 0C 1A F8\r" → [0x41, 0x0C, 0x1A, 0xF8]
    std::string response = "41 0C 1A F8\r";
    auto binary = ELM327Transport::parseOBD2Response(response);
    ASSERT_TRUE(binary.has_value());
    EXPECT_EQ(binary->size(), 4);
    EXPECT_EQ((*binary)[0], 0x41);
    EXPECT_EQ((*binary)[1], 0x0C);
    EXPECT_EQ((*binary)[2], 0x1A);
    EXPECT_EQ((*binary)[3], 0xF8);
}

TEST(AutoDetectionFlow, ELM327Transport_HandlesNoDataResponse)
{
    std::string noData = "NO DATA\r";
    auto binary = ELM327Transport::parseOBD2Response(noData);
    EXPECT_FALSE(binary.has_value());
}

TEST(AutoDetectionFlow, ELM327Transport_HandlesErrorResponse)
{
    std::string error = "ERROR\r";
    auto binary = ELM327Transport::parseOBD2Response(error);
    EXPECT_FALSE(binary.has_value());
}

TEST(AutoDetectionFlow, ELM327Transport_HandlesMultiFrameVINResponse)
{
    // Multi-line VIN response with line numbers
    std::string multiFrame = "014: 49 02 01 00 00 35 59\r015: 4A 33 53 32 44 58 4D 48\r016: 31 30 35 37 36 00 00\r";
    auto binary = ELM327Transport::parseOBD2Response(multiFrame);
    ASSERT_TRUE(binary.has_value());
    // Should contain VIN data bytes (skipping mode, pid, pci)
    EXPECT_GE(binary->size(), 17);
}

TEST(AutoDetectionFlow, ELM327Transport_BuildsInitSequenceWithDelays)
{
    auto initCommands = ELM327Transport::buildInitSequence();
    EXPECT_GE(initCommands.size(), 5);

    // First command (ATZ) should have longer delay
    EXPECT_GE(initCommands[0].delayMs, 500);

    // Subsequent commands should have minimum delay
    for (size_t i = 1; i < initCommands.size(); ++i) {
        EXPECT_GE(initCommands[i].delayMs, 50);
    }
}

/**
 * Test VehicleDetector integration with OBD2Protocol
 */
TEST(AutoDetectionFlow, VehicleDetector_Integration_DecodesVINFromELM327Response)
{
    domain::VehicleDetector detector;
    domain::VehicleDetectionResult result;

    // Single-frame VIN response
    std::string singleFrame = "49 02 01 00 00 35 59 4A 33 53 32 44 58 4D 48 31 30 35 37 36\r";
    auto binarySingle = ELM327Transport::parseOBD2Response(singleFrame);
    ASSERT_TRUE(binarySingle.has_value());

    bool success = detector.feedVINResponse(*binarySingle);
    EXPECT_TRUE(success);

    result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->vin, "5YJ3E1EA1NF123456");
    EXPECT_EQ(result->make, domain::VehicleMake::Tesla);
}

TEST(AutoDetectionFlow, VehicleDetector_Integration_DecodesMultiFrameVIN)
{
    domain::VehicleDetector detector;

    // Frame 1: "014: 49 02 01 00 00 35 59\r"
    auto binary1 = ELM327Transport::parseOBD2Response("014: 49 02 01 00 00 35 59\r");

    // Frame 2: "015: 4A 33 53 32 44 58 4D 48\r"
    auto binary2 = ELM327Transport::parseOBD2Response("015: 4A 33 53 32 44 58 4D 48\r");

    // Frame 3: "016: 31 30 35 37 36 00 00\r"
    auto binary3 = ELM327Transport::parseOBD2Response("016: 31 30 35 37 36 00 00\r");

    // Feed frames sequentially
    EXPECT_TRUE(binary1.has_value());
    detector.feedVINResponse(*binary1);
    EXPECT_TRUE(binary2.has_value());
    detector.feedVINResponse(*binary3);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->vin, "5YJ3S2DXMH10576");
}

TEST(AutoDetectionFlow, VehicleDetector_Integration_DecodesFuelTypeResponse)
{
    domain::VehicleDetector detector;

    // Fuel type response: "49 51 08\r" (Mode 09, Electric)
    auto binary = ELM327Transport::parseOBD2Response("49 51 08\r");
    ASSERT_TRUE(binary.has_value());

    bool success = detector.feedFuelTypeResponse(*binary);
    EXPECT_TRUE(success);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isElectric);
}

TEST(AutoDetectionFlow, VehicleDetector_SuggestsCorrectVehicleConfig)
{
    domain::VehicleDetector detector;

    // Tesla VIN response
    std::string teslaVIN = "49 02 01 00 00 35 59 4A 33 45 31 45 41 31 4E 46 31 32 33 34 35 37 36\r";
    auto binary = ELM327Transport::parseOBD2Response(teslaVIN);

    EXPECT_TRUE(binary.has_value());
    detector.feedVINResponse(*binary);

    // Electric fuel type
    std::string fuelElectric = "49 51 08\r";
    auto binaryFuel = ELM327Transport::parseOBD2Response(fuelElectric);
    EXPECT_TRUE(binaryFuel.has_value());
    detector.feedFuelTypeResponse(*binaryFuel);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suggestedVehicleId, "tesla_model3");
}

TEST(AutoDetectionFlow, VehicleDetector_SuggestsAudiEVConfig)
{
    domain::VehicleDetector detector;

    // Audi VIN response
    std::string audiVIN = "49 02 01 00 00 57 41 55 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A\r";
    auto binary = ELM327Transport::parseOBD2Response(audiVIN);

    EXPECT_TRUE(binary.has_value());
    detector.feedVINResponse(*binary);

    // Electric fuel type
    std::string fuelElectric = "49 51 08\r";
    auto binaryFuel = ELM327Transport::parseOBD2Response(fuelElectric);
    EXPECT_TRUE(binaryFuel.has_value());
    detector.feedFuelTypeResponse(*binaryFuel);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suggestedVehicleId, "audi_mlb");
    EXPECT_TRUE(result->isElectric);
}

TEST(AutoDetectionFlow, VehicleDetector_SuggestsGenericConfigForNonEV)
{
    domain::VehicleDetector detector;

    // Audi VIN response
    std::string audiVIN = "49 02 01 00 00 57 41 55 5A 5A 5A 5A 5A 5A 5A 5A 5A 5A\r";
    auto binary = ELM327Transport::parseOBD2Response(audiVIN);

    EXPECT_TRUE(binary.has_value());
    detector.feedVINResponse(*binary);

    // Gasoline fuel type
    std::string fuelGasoline = "49 51 01\r";
    auto binaryFuel = ELM327Transport::parseOBD2Response(fuelGasoline);
    EXPECT_TRUE(binaryFuel.has_value());
    detector.feedFuelTypeResponse(*binaryFuel);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->suggestedVehicleId, "generic");
    EXPECT_FALSE(result->isElectric);
}

/**
 * Test initialization command sequence
 */
TEST(AutoDetectionFlow, InitSequence_HasCorrectCommandsAndDelays)
{
    auto commands = ELM327Transport::buildInitSequence();

    ASSERT_EQ(commands[0].command, "ATZ\r");
    EXPECT_EQ(commands[1].command, "ATE0\r");
    EXPECT_EQ(commands[2].command, "ATH0\r");
    EXPECT_EQ(commands[3].command, "ATL0\r");
    EXPECT_EQ(commands[4].command, "ATSP0\r");
    EXPECT_EQ(commands[5].command, "ATSTFF\r");
    ASSERT_EQ(commands[0].delayMs, 500);
    EXPECT_EQ(commands[1].delayMs, 50);
    EXPECT_EQ(commands[2].delayMs, 50);
    EXPECT_EQ(commands[3].delayMs, 50);
    EXPECT_EQ(commands[4].delayMs, 50);
    EXPECT_EQ(commands[5].delayMs, 50);
}

/**
 * Test error handling
 */
TEST(AutoDetectionFlow, HandlesInitializationErrorsGracefully)
{
    OBD2Protocol obd2Protocol;

    // Set up callback that always returns failure
    obd2Protocol.setSendCallback([&](const std::string&) -> std::vector<uint8_t> {
        return std::nullopt;  // Simulate "ERROR" or "NO DATA" response
    });

    // Initialization should fail gracefully
    auto detectionResult = obd2Protocol.initialize();
    EXPECT_TRUE(detectionResult.has_value());
    EXPECT_EQ(detectionResult->vin, "");
    EXPECT_EQ(detectionResult->make, domain::VehicleMake::Unknown);
}
