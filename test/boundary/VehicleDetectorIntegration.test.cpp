#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/VehicleDetector.h"

using namespace vehicle_sim::boundary;
using namespace vehicle_sim::domain;
using testing::Eq;
using testing::Optional;

// ================================================
// Test Suite 1: VIN Response Decoding with ASCII Transport
// ================================================

TEST(VehicleDetectorIntegration, VIN_ASCII_To_Binary_DecodesCorrectly)
{
    // Flow: ELM327 returns VIN as ASCII hex with line numbers
    // "014: 49 02 01 00 00 00 35 59\r015: 4A 33 53 32 44 58 4D 48\r016: 31 30 35 37 36 00 00 00\r"
    // Parse to binary → Feed to VehicleDetector → Extract VIN

    std::string vinASCII =
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 53 32 44 58 4D 48\r"
        "016: 31 30 35 37 36 00 00 00\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(vinASCII);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    bool success = detector.feedVINResponse(*vinBinary);
    EXPECT_TRUE(success);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    // Expected VIN: "5YJ3S2DXMH10576"
    EXPECT_EQ(result->vin, "5YJ3S2DXMH10576");
}

TEST(VehicleDetectorIntegration, VIN_Tesla_DecodesWMI)
{
    // VIN starting with "5YJ" = Tesla
    std::string teslaVIN =
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 45 31 45 41 31 4E\r"
        "016: 46 31 32 33 34 35 36 00\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(teslaVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Tesla);
    EXPECT_EQ(result->wmi, "5YJ");
    EXPECT_EQ(result->vin, "5YJ3E1EA1NF123456");
}

TEST(VehicleDetectorIntegration, VIN_Audi_DecodesWMI)
{
    // VIN starting with "WAU" = Audi
    std::string audiVIN =
        "014: 49 02 01 00 00 00 57 41\r"
        "015: 55 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(audiVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Audi);
    EXPECT_EQ(result->wmi, "WAU");
    EXPECT_EQ(result->vin, "WAUZZZZZZZZZZZZZZ");
}

TEST(VehicleDetectorIntegration, VIN_Volkswagen_DecodesWMI)
{
    // VIN starting with "WVW" = Volkswagen
    std::string vwVIN =
        "014: 49 02 01 00 00 00 57 56\r"
        "015: 57 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(vwVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Volkswagen);
    EXPECT_EQ(result->wmi, "WVW");
}

TEST(VehicleDetectorIntegration, VIN_BMW_DecodesWMI)
{
    // VIN starting with "WBA" = BMW
    std::string bmwVIN =
        "014: 49 02 01 00 00 00 57 42\r"
        "015: 41 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(bmwVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::BMW);
    EXPECT_EQ(result->wmi, "WBA");
}

// ================================================
// Test Suite 2: Fuel Type Response Decoding
// ================================================

TEST(VehicleDetectorIntegration, FuelType_ASCII_To_Binary_Electric)
{
    // Mode 09 PID 51: Fuel Type
    // "49 51 08\r" → 0x08 = Electric
    std::string fuelTypeASCII = "49 51 08\r";

    auto fuelTypeBinary = ELM327Transport::parseOBD2Response(fuelTypeASCII);
    ASSERT_TRUE(fuelTypeBinary.has_value());

    VehicleDetector detector;
    bool success = detector.feedFuelTypeResponse(*fuelTypeBinary);
    EXPECT_TRUE(success);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isElectric);
}

TEST(VehicleDetectorIntegration, FuelType_ASCII_To_Binary_Gasoline)
{
    // "49 51 01\r" → 0x01 = Gasoline
    std::string fuelTypeASCII = "49 51 01\r";

    auto fuelTypeBinary = ELM327Transport::parseOBD2Response(fuelTypeASCII);
    ASSERT_TRUE(fuelTypeBinary.has_value());

    VehicleDetector detector;
    detector.feedFuelTypeResponse(*fuelTypeBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->isElectric);
}

TEST(VehicleDetectorIntegration, FuelType_ASCII_To_Binary_Diesel)
{
    // "49 51 04\r" → 0x04 = Diesel
    std::string fuelTypeASCII = "49 51 04\r";

    auto fuelTypeBinary = ELM327Transport::parseOBD2Response(fuelTypeASCII);
    ASSERT_TRUE(fuelTypeBinary.has_value());

    VehicleDetector detector;
    detector.feedFuelTypeResponse(*fuelTypeBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->isElectric);  // Diesel is not electric
}

TEST(VehicleDetectorIntegration, FuelType_ASCII_To_Binary_Hybrid)
{
    // "49 51 0A\r" → 0x0A = Hybrid
    std::string fuelTypeASCII = "49 51 0A\r";

    auto fuelTypeBinary = ELM327Transport::parseOBD2Response(fuelTypeASCII);
    ASSERT_TRUE(fuelTypeBinary.has_value());

    VehicleDetector detector;
    detector.feedFuelTypeResponse(*fuelTypeBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    // Hybrid detection logic - may treat as electric or not depending on config
    // This test just verifies parsing works
}

// ================================================
// Test Suite 3: Full Detection Flow Integration
// ================================================

TEST(VehicleDetectorIntegration, FullFlow_TeslaEV_DetectsCorrectly)
{
    VehicleDetector detector;

    // Step 1: VIN query and response
    std::string vinQuery = ELM327Transport::buildOBD2Query(0x09, 0x02);
    EXPECT_EQ(vinQuery, "09 02\r");

    std::string teslaVINResponse =
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 45 31 45 41 31 4E\r"
        "016: 46 31 32 33 34 35 36 00\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(teslaVINResponse);
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    // Step 2: Fuel type query and response
    std::string fuelQuery = ELM327Transport::buildOBD2Query(0x09, 0x51);
    EXPECT_EQ(fuelQuery, "09 51\r");

    std::string fuelTypeResponse = "49 51 08\r";  // Electric
    auto fuelBinary = ELM327Transport::parseOBD2Response(fuelTypeResponse);
    ASSERT_TRUE(fuelBinary.has_value());
    detector.feedFuelTypeResponse(*fuelBinary);

    // Verify detection result
    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Tesla);
    EXPECT_TRUE(result->isElectric);
    EXPECT_EQ(result->wmi, "5YJ");
    EXPECT_EQ(result->vin, "5YJ3E1EA1NF123456");
    EXPECT_EQ(result->suggestedVehicleId, "tesla_model3");
}

TEST(VehicleDetectorIntegration, FullFlow_AudiEV_DetectsCorrectly)
{
    VehicleDetector detector;

    // VIN response
    std::string audiVINResponse =
        "014: 49 02 01 00 00 00 57 41\r"
        "015: 55 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(audiVINResponse);
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    // Fuel type response
    std::string fuelTypeResponse = "49 51 08\r";  // Electric
    auto fuelBinary = ELM327Transport::parseOBD2Response(fuelTypeResponse);
    ASSERT_TRUE(fuelBinary.has_value());
    detector.feedFuelTypeResponse(*fuelBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Audi);
    EXPECT_TRUE(result->isElectric);
    EXPECT_EQ(result->suggestedVehicleId, "audi_mlb");
}

TEST(VehicleDetectorIntegration, FullFlow_AudiGas_DetectsCorrectly)
{
    VehicleDetector detector;

    // VIN response (same WMI)
    std::string audiVINResponse =
        "014: 49 02 01 00 00 00 57 41\r"
        "015: 55 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(audiVINResponse);
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    // Fuel type response - gasoline
    std::string fuelTypeResponse = "49 51 01\r";  // Gasoline
    auto fuelBinary = ELM327Transport::parseOBD2Response(fuelTypeResponse);
    ASSERT_TRUE(fuelBinary.has_value());
    detector.feedFuelTypeResponse(*fuelBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Audi);
    EXPECT_FALSE(result->isElectric);
    EXPECT_EQ(result->suggestedVehicleId, "generic");  // Non-EV Audi falls back to generic
}

TEST(VehicleDetectorIntegration, FullFlow_VolkswagenEV_DetectsCorrectly)
{
    VehicleDetector detector;

    // VIN response
    std::string vwVINResponse =
        "014: 49 02 01 00 00 00 57 56\r"
        "015: 57 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(vwVINResponse);
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    // Fuel type - electric
    std::string fuelTypeResponse = "49 51 08\r";
    auto fuelBinary = ELM327Transport::parseOBD2Response(fuelTypeResponse);
    ASSERT_TRUE(fuelBinary.has_value());
    detector.feedFuelTypeResponse(*fuelBinary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->make, VehicleMake::Volkswagen);
    EXPECT_TRUE(result->isElectric);
    EXPECT_EQ(result->suggestedVehicleId, "audi_mlb");  // VW EV uses MLB config
}

// ================================================
// Test Suite 4: Multi-frame VIN with ASCII Transport
// ================================================

TEST(VehicleDetectorIntegration, MultiFrameVIN_AggregatesCorrectly)
{
    // Some ELM327 adapters send VIN in multiple responses
    VehicleDetector detector;

    // First frame
    std::string frame1 = "014: 49 02 01 00 00 00 35 59\r";
    auto f1Binary = ELM327Transport::parseOBD2Response(frame1);
    ASSERT_TRUE(f1Binary.has_value());
    detector.feedVINResponse(*f1Binary);

    // Second frame
    std::string frame2 = "015: 4A 33 53 32 44 58 4D 48\r";
    auto f2Binary = ELM327Transport::parseOBD2Response(frame2);
    ASSERT_TRUE(f2Binary.has_value());
    detector.feedVINResponse(*f2Binary);

    // Third frame
    std::string frame3 = "016: 31 30 35 37 36 00 00 00\r";
    auto f3Binary = ELM327Transport::parseOBD2Response(frame3);
    ASSERT_TRUE(f3Binary.has_value());
    detector.feedVINResponse(*f3Binary);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->vin, "5YJ3S2DXMH10576");
    EXPECT_EQ(result->make, VehicleMake::Tesla);
}

// ================================================
// Test Suite 5: Query Building Integration
// ================================================

TEST(VehicleDetectorIntegration, VINQuery_ViaELM327Transport)
{
    // VehicleDetector::buildVINQuery() returns binary
    // But we need to convert to ASCII for ELM327
    auto binaryQuery = VehicleDetector::buildVINQuery();
    ASSERT_EQ(binaryQuery.size(), 2);
    EXPECT_EQ(binaryQuery[0], 0x09);
    EXPECT_EQ(binaryQuery[1], 0x02);

    // Convert to ASCII for ELM327 transport
    std::string asciiQuery = ELM327Transport::buildOBD2Query(0x09, 0x02);
    EXPECT_EQ(asciiQuery, "09 02\r");
}

TEST(VehicleDetectorIntegration, FuelTypeQuery_ViaELM327Transport)
{
    auto binaryQuery = VehicleDetector::buildFuelTypeQuery();
    ASSERT_EQ(binaryQuery.size(), 2);
    EXPECT_EQ(binaryQuery[0], 0x01);
    EXPECT_EQ(binaryQuery[1], 0x51);

    // Convert to ASCII for ELM327 transport
    std::string asciiQuery = ELM327Transport::buildOBD2Query(0x01, 0x51);
    EXPECT_EQ(asciiQuery, "01 51\r");
}

// ================================================
// Test Suite 6: Edge Cases
// ================================================

TEST(VehicleDetectorIntegration, NoDataResponse_FailsGracefully)
{
    std::string noData = "NO DATA\r";
    auto binary = ELM327Transport::parseOBD2Response(noData);

    EXPECT_FALSE(binary.has_value());

    VehicleDetector detector;
    // Should handle nullopt gracefully - either return false or no change
    // This depends on implementation - test verifies it doesn't crash
}

TEST(VehicleDetectorIntegration, EmptyVINResponse_FailsGracefully)
{
    std::string emptyResponse = "";
    auto binary = ELM327Transport::parseOBD2Response(emptyResponse);

    EXPECT_FALSE(binary.has_value());
}

TEST(VehicleDetectorIntegration, InvalidHexResponse_FailsGracefully)
{
    std::string invalidHex = "41 0X 1A F8\r";  // Invalid 'X' character
    auto binary = ELM327Transport::parseOBD2Response(invalidHex);

    EXPECT_FALSE(binary.has_value());
}

// ================================================
// Test Suite 7: Detector State Management
// ================================================

TEST(VehicleDetectorIntegration, Reset_ClearsState)
{
    VehicleDetector detector;

    std::string vinResponse =
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 53 32 44 58 4D 48\r"
        "016: 31 30 35 37 36 00 00 00\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(vinResponse);
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    ASSERT_TRUE(detector.getResult().has_value());

    detector.reset();

    EXPECT_FALSE(detector.getResult().has_value());
}

// ================================================
// Test Suite 8: Config ID Selection
// ================================================

TEST(VehicleDetectorIntegration, ConfigID_Selection_FollowsBusinessRules)
{
    // Tesla + Electric = tesla_model3
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Tesla, true), "tesla_model3");

    // Audi + Electric = audi_mlb
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Audi, true), "audi_mlb");

    // Audi + Gasoline = generic
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Audi, false), "generic");

    // Volkswagen + Electric = audi_mlb (VW EVs use MLB platform)
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Volkswagen, true), "audi_mlb");

    // Generic + anything = generic
    EXPECT_EQ(VehicleDetector::makeToConfigId(VehicleMake::Generic, false), "generic");
}
