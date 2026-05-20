#include <gtest/gtest.h>
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/VehicleDetector.h"

using namespace vehicle_sim::boundary;
using namespace vehicle_sim::domain;

// ================================================
// Test Suite 1: VIN Response Decoding with ASCII Transport
// ================================================

TEST(VehicleDetectorIntegration, VIN_ASCII_To_Binary_DecodesCorrectly)
{
    std::string vinASCII =
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 53 32 44 58 4D 48\r"
        "016: 31 30 35 37 36 00 00 00\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(vinASCII);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    EXPECT_TRUE(detector.feedVINResponse(*vinBinary));

    auto result = detector.getResult();
    EXPECT_EQ(result.vin, "5YJ3S2DXMH10576");
}

TEST(VehicleDetectorIntegration, VIN_Tesla_DecodesWMI)
{
    std::string teslaVIN =
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 45 31 45 41 31 4E\r"
        "016: 46 31 32 33 34 35 36 00\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(teslaVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Tesla, result.make);
    EXPECT_EQ("5YJ", result.wmi);
    EXPECT_EQ("5YJ3E1EA1NF123456", result.vin);
}

TEST(VehicleDetectorIntegration, VIN_Audi_DecodesWMI)
{
    std::string audiVIN =
        "014: 49 02 01 00 00 00 57 41\r"
        "015: 55 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(audiVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Audi, result.make);
    EXPECT_EQ("WAU", result.wmi);
}

TEST(VehicleDetectorIntegration, VIN_Volkswagen_DecodesWMI)
{
    std::string vwVIN =
        "014: 49 02 01 00 00 00 57 56\r"
        "015: 57 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(vwVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Volkswagen, result.make);
    EXPECT_EQ("WVW", result.wmi);
}

TEST(VehicleDetectorIntegration, VIN_BMW_DecodesWMI)
{
    std::string bmwVIN =
        "014: 49 02 01 00 00 00 57 42\r"
        "015: 41 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r";

    auto vinBinary = ELM327Transport::parseOBD2Response(bmwVIN);
    ASSERT_TRUE(vinBinary.has_value());

    VehicleDetector detector;
    detector.feedVINResponse(*vinBinary);

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::BMW, result.make);
    EXPECT_EQ("WBA", result.wmi);
}

// ================================================
// Test Suite 2: Fuel Type Response Decoding
// ================================================

TEST(VehicleDetectorIntegration, FuelType_ASCII_To_Binary_Electric)
{
    auto fuelTypeBinary = ELM327Transport::parseOBD2Response("49 51 08\r");
    ASSERT_TRUE(fuelTypeBinary.has_value());

    VehicleDetector detector;
    EXPECT_TRUE(detector.feedFuelTypeResponse(*fuelTypeBinary));
    EXPECT_TRUE(detector.getResult().isElectric);
}

TEST(VehicleDetectorIntegration, FuelType_ASCII_To_Binary_Gasoline)
{
    auto fuelTypeBinary = ELM327Transport::parseOBD2Response("49 51 01\r");
    ASSERT_TRUE(fuelTypeBinary.has_value());

    VehicleDetector detector;
    detector.feedFuelTypeResponse(*fuelTypeBinary);
    EXPECT_FALSE(detector.getResult().isElectric);
}

// ================================================
// Test Suite 3: Full Detection Flow Integration
// ================================================

TEST(VehicleDetectorIntegration, FullFlow_TeslaEV_DetectsCorrectly)
{
    VehicleDetector detector;

    auto vinBinary = ELM327Transport::parseOBD2Response(
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 45 31 45 41 31 4E\r"
        "016: 46 31 32 33 34 35 36 00\r");
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    auto fuelBinary = ELM327Transport::parseOBD2Response("49 51 08\r");
    ASSERT_TRUE(fuelBinary.has_value());
    detector.feedFuelTypeResponse(*fuelBinary);

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Tesla, result.make);
    EXPECT_TRUE(result.isElectric);
    EXPECT_EQ("tesla", result.suggestedVehicleId);
}

TEST(VehicleDetectorIntegration, FullFlow_AudiEV_DetectsCorrectly)
{
    VehicleDetector detector;

    auto vinBinary = ELM327Transport::parseOBD2Response(
        "014: 49 02 01 00 00 00 57 41\r"
        "015: 55 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r");
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    detector.feedFuelTypeResponse(ELM327Transport::parseOBD2Response("49 51 08\r").value());

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Audi, result.make);
    EXPECT_TRUE(result.isElectric);
    EXPECT_EQ("audi_mlb_evo", result.suggestedVehicleId);
}

TEST(VehicleDetectorIntegration, FullFlow_AudiGas_FallsBackToGeneric)
{
    VehicleDetector detector;

    auto vinBinary = ELM327Transport::parseOBD2Response(
        "014: 49 02 01 00 00 00 57 41\r"
        "015: 55 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r");
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    detector.feedFuelTypeResponse(ELM327Transport::parseOBD2Response("49 51 01\r").value());

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Audi, result.make);
    EXPECT_FALSE(result.isElectric);
    EXPECT_EQ("generic", result.suggestedVehicleId);
}

TEST(VehicleDetectorIntegration, FullFlow_VolkswagenEV_UsesMLBConfig)
{
    VehicleDetector detector;

    auto vinBinary = ELM327Transport::parseOBD2Response(
        "014: 49 02 01 00 00 00 57 56\r"
        "015: 57 5A 5A 5A 5A 5A 5A 5A\r"
        "016: 5A 5A 5A 5A 5A 5A 5A 5A\r");
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);

    detector.feedFuelTypeResponse(ELM327Transport::parseOBD2Response("49 51 08\r").value());

    auto result = detector.getResult();
    EXPECT_EQ(VehicleMake::Volkswagen, result.make);
    EXPECT_EQ("audi_mlb_evo", result.suggestedVehicleId);
}

// ================================================
// Test Suite 4: Multi-frame VIN with ASCII Transport
// ================================================

TEST(VehicleDetectorIntegration, MultiFrameVIN_AggregatesCorrectly)
{
    VehicleDetector detector;

    auto f1 = ELM327Transport::parseOBD2Response("014: 49 02 01 00 00 00 35 59\r");
    ASSERT_TRUE(f1.has_value());
    detector.feedVINResponse(*f1);

    auto f2 = ELM327Transport::parseOBD2Response("015: 4A 33 53 32 44 58 4D 48\r");
    ASSERT_TRUE(f2.has_value());
    detector.feedVINResponse(*f2);

    auto f3 = ELM327Transport::parseOBD2Response("016: 31 30 35 37 36 00 00 00\r");
    ASSERT_TRUE(f3.has_value());
    detector.feedVINResponse(*f3);

    auto result = detector.getResult();
    EXPECT_EQ("5YJ3S2DXMH10576", result.vin);
    EXPECT_EQ(VehicleMake::Tesla, result.make);
}

// ================================================
// Test Suite 5: Edge Cases
// ================================================

TEST(VehicleDetectorIntegration, NoDataResponse_DoesNotCrash)
{
    auto binary = ELM327Transport::parseOBD2Response("NO DATA\r");
    EXPECT_FALSE(binary.has_value());
    // Verifies graceful handling — no crash
}

TEST(VehicleDetectorIntegration, Reset_ClearsVINState)
{
    VehicleDetector detector;
    auto vinBinary = ELM327Transport::parseOBD2Response(
        "014: 49 02 01 00 00 00 35 59\r"
        "015: 4A 33 53 32 44 58 4D 48\r"
        "016: 31 30 35 37 36 00 00 00\r");
    ASSERT_TRUE(vinBinary.has_value());
    detector.feedVINResponse(*vinBinary);
    ASSERT_FALSE(detector.getResult().vin.empty());

    detector.reset();
    EXPECT_TRUE(detector.getResult().vin.empty());
    EXPECT_FALSE(detector.getResult().hasSuggestion());
}

// ================================================
// Test Suite 6: Config ID Selection
// ================================================

TEST(VehicleDetectorIntegration, ConfigID_Selection_FollowsBusinessRules)
{
    EXPECT_EQ("tesla", VehicleDetector::makeToConfigId(VehicleMake::Tesla, true));
    EXPECT_EQ("audi_mlb_evo", VehicleDetector::makeToConfigId(VehicleMake::Audi, true));
    EXPECT_EQ("generic", VehicleDetector::makeToConfigId(VehicleMake::Audi, false));
    EXPECT_EQ("audi_mlb_evo", VehicleDetector::makeToConfigId(VehicleMake::Volkswagen, true));
    EXPECT_EQ("generic", VehicleDetector::makeToConfigId(VehicleMake::Generic, false));
}
