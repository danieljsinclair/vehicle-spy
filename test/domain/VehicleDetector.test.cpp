#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/domain/VehicleDetector.h"

using namespace vehicle_sim::domain;
using testing::Eq;

// ================================================
// Test Suite 1: WMI Decoding Tests
// ================================================

TEST(VehicleDetector, DecodeWMI_Tesla5YJ)
{
    auto make = VehicleDetector::decodeWMI("5YJ");
    EXPECT_EQ(make, VehicleMake::Tesla);
}

TEST(VehicleDetector, DecodeWMI_Tesla7SA)
{
    auto make = VehicleDetector::decodeWMI("7SA");
    EXPECT_EQ(make, VehicleMake::Tesla);
}

TEST(VehicleDetector, DecodeWMI_AudiWAU)
{
    auto make = VehicleDetector::decodeWMI("WAU");
    EXPECT_EQ(make, VehicleMake::Audi);
}

TEST(VehicleDetector, DecodeWMI_AudiWA1)
{
    auto make = VehicleDetector::decodeWMI("WA1");
    EXPECT_EQ(make, VehicleMake::Audi);
}

TEST(VehicleDetector, DecodeWMI_AudiTRU)
{
    auto make = VehicleDetector::decodeWMI("TRU");
    EXPECT_EQ(make, VehicleMake::Audi);
}

TEST(VehicleDetector, DecodeWMI_VolkswagenWVW)
{
    auto make = VehicleDetector::decodeWMI("WVW");
    EXPECT_EQ(make, VehicleMake::Volkswagen);
}

TEST(VehicleDetector, DecodeWMI_BMWWBA)
{
    auto make = VehicleDetector::decodeWMI("WBA");
    EXPECT_EQ(make, VehicleMake::BMW);
}

TEST(VehicleDetector, DecodeWMI_Unknown)
{
    auto make = VehicleDetector::decodeWMI("XYZ");
    EXPECT_EQ(make, VehicleMake::Unknown);
}

TEST(VehicleDetector, DecodeWMI_Generic)
{
    auto make = VehicleDetector::decodeWMI("1G1");
    EXPECT_EQ(make, VehicleMake::Generic);
}

// ================================================
// Test Suite 2: Config ID Mapping Tests
// ================================================

TEST(VehicleDetector, MakeToConfigId_TeslaElectric)
{
    auto configId = VehicleDetector::makeToConfigId(VehicleMake::Tesla, true);
    EXPECT_EQ(configId, "tesla_model3");
}

TEST(VehicleDetector, MakeToConfigId_AudiElectric)
{
    auto configId = VehicleDetector::makeToConfigId(VehicleMake::Audi, true);
    EXPECT_EQ(configId, "audi_mlb");
}

TEST(VehicleDetector, MakeToConfigId_AudiGasoline)
{
    auto configId = VehicleDetector::makeToConfigId(VehicleMake::Audi, false);
    EXPECT_EQ(configId, "generic");
}

TEST(VehicleDetector, MakeToConfigId_VolkswagenElectric)
{
    auto configId = VehicleDetector::makeToConfigId(VehicleMake::Volkswagen, true);
    EXPECT_EQ(configId, "audi_mlb");
}

TEST(VehicleDetector, MakeToConfigId_Generic)
{
    auto configId = VehicleDetector::makeToConfigId(VehicleMake::Generic, false);
    EXPECT_EQ(configId, "generic");
}

// ================================================
// Test Suite 3: VIN Extraction Tests
// ================================================

TEST(VehicleDetector, FeedVINResponse_SingleFrame)
{
    VehicleDetector detector;
    std::vector<uint8_t> response = {
        0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x35, 0x59,
        0x4A, 0x33, 0x53, 0x32, 0x44, 0x58, 0x4D, 0x48,
        0x31, 0x30, 0x35, 0x37, 0x36
    };
    bool success = detector.feedVINResponse(response);
    EXPECT_TRUE(success);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->vin, "5YJ3S2DXMH10576");
}

TEST(VehicleDetector, FeedVINResponse_MultiFrame)
{
    VehicleDetector detector;

    std::vector<uint8_t> frame1 = {0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x57, 0x41};
    std::vector<uint8_t> frame2 = {0x49, 0x02, 0x02, 0x55, 0x5A, 0x5A, 0x5A, 0x5A};
    std::vector<uint8_t> frame3 = {0x49, 0x02, 0x03, 0x5A, 0x31, 0x32, 0x33, 0x34};
    std::vector<uint8_t> frame4 = {0x49, 0x02, 0x04, 0x35, 0x36, 0x37, 0x00, 0x00};

    EXPECT_TRUE(detector.feedVINResponse(frame1));
    EXPECT_TRUE(detector.feedVINResponse(frame2));
    EXPECT_TRUE(detector.feedVINResponse(frame3));
    EXPECT_TRUE(detector.feedVINResponse(frame4));

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->vin, "WAUZZZZZ1234567");
}

TEST(VehicleDetector, ExtractVINFromResponse_EmptyResponse)
{
    std::vector<uint8_t> empty;
    auto vin = VehicleDetector::extractVINFromResponse(empty);
    EXPECT_TRUE(vin.empty());
}

// ================================================
// Test Suite 4: Fuel Type Tests
// ================================================

TEST(VehicleDetector, FeedFuelTypeResponse_Electric)
{
    VehicleDetector detector;
    std::vector<uint8_t> response = {0x49, 0x51, 0x08};
    bool success = detector.feedFuelTypeResponse(response);
    EXPECT_TRUE(success);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isElectric);
}

TEST(VehicleDetector, FeedFuelTypeResponse_Gasoline)
{
    VehicleDetector detector;
    std::vector<uint8_t> response = {0x49, 0x51, 0x01};
    bool success = detector.feedFuelTypeResponse(response);
    EXPECT_TRUE(success);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->isElectric);
}

// ================================================
// Test Suite 5: Full Detection Flow Tests
// ================================================

TEST(VehicleDetector, FullDetectionFlow_TeslaEV)
{
    VehicleDetector detector;

    std::vector<uint8_t> teslaVIN = {0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x35, 0x59, 0x4A, 0x33, 0x45, 0x31, 0x45, 0x41, 0x31, 0x4E, 0x46, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36};
    detector.feedVINResponse(teslaVIN);

    std::vector<uint8_t> fuelType = {0x41, 0x51, 0x08};
    detector.feedFuelTypeResponse(fuelType);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->make, VehicleMake::Tesla);
    EXPECT_TRUE(result->isElectric);
    EXPECT_EQ(result->suggestedVehicleId, "tesla_model3");
    EXPECT_EQ(result->vin, "5YJ3E1EA1NF123456");
}

TEST(VehicleDetector, FullDetectionFlow_AudiEV)
{
    VehicleDetector detector;

    std::vector<uint8_t> audiVIN = {0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x57, 0x41, 0x55, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};
    detector.feedVINResponse(audiVIN);

    std::vector<uint8_t> fuelType = {0x41, 0x51, 0x08};
    detector.feedFuelTypeResponse(fuelType);

    auto result = detector.getResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->make, VehicleMake::Audi);
    EXPECT_TRUE(result->isElectric);
    EXPECT_EQ(result->suggestedVehicleId, "audi_mlb");
}

// ================================================
// Test Suite 6: State Management Tests
// ================================================

TEST(VehicleDetector, Reset)
{
    VehicleDetector detector;

    std::vector<uint8_t> vinResponse = {0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x35, 0x59, 0x4A, 0x33, 0x53, 0x32, 0x44, 0x58, 0x4D, 0x48, 0x31, 0x30, 0x35, 0x37, 0x36};
    detector.feedVINResponse(vinResponse);

    ASSERT_TRUE(detector.getResult().has_value());

    detector.reset();

    EXPECT_FALSE(detector.getResult().has_value());
}

// ================================================
// Test Suite 7: Build Query Tests
// ================================================

TEST(VehicleDetector, BuildVINQuery)
{
    auto query = VehicleDetector::buildVINQuery();
    ASSERT_EQ(query.size(), 2);
    EXPECT_EQ(query[0], 0x09);
    EXPECT_EQ(query[1], 0x02);
}

TEST(VehicleDetector, BuildFuelTypeQuery)
{
    auto query = VehicleDetector::buildFuelTypeQuery();
    ASSERT_EQ(query.size(), 2);
    EXPECT_EQ(query[0], 0x01);
    EXPECT_EQ(query[1], 0x51);
}
