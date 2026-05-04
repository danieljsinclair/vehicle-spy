#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/OBD2Math.h"

using namespace vehicle_sim::boundary;
using namespace vehicle_sim::domain;
using testing::Eq;
using testing::Optional;
using testing::DoubleNear;

// ================================================
// Test Suite 1: RPM Signal Extraction End-to-End
// ================================================

TEST(OBD2EndToEnd, RPM_ASCII_To_VehicleSignal_CalculatesCorrectly)
{
    // Flow: Query PID 0x0C → ELM327 returns "41 0C 1A F8\r"
    // Parse to binary → Apply SAE J1979 formula: RPM = ((A * 256) + B) / 4
    // 0x1A = 26, 0xF8 = 248
    // RPM = ((26 * 256) + 248) / 4 = (6656 + 248) / 4 = 6904 / 4 = 1726
    // Wait, let me recalculate:
    // 0x1A = 26, 0xF8 = 248
    // RPM = ((26 * 256) + 248) / 4 = (6656 + 248) / 4 = 6904 / 4 = 1726

    std::string elmResponse = "41 0C 1A F8\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 4);

    // Validate structure: [mode=0x41, pid=0x0C, dataA=0x1A, dataB=0xF8]
    EXPECT_EQ((*binaryData)[0], 0x41);  // Mode 01 response
    EXPECT_EQ((*binaryData)[1], 0x0C);  // PID 0x0C (RPM)

    uint8_t msb = (*binaryData)[2];  // 0x1A = 26
    uint8_t lsb = (*binaryData)[3];  // 0xF8 = 248

    double rpm = obd2WordRPM(msb, lsb);
    EXPECT_EQ(rpm, 1726.0);
}

TEST(OBD2EndToEnd, RPM_LowValue_CalculatesCorrectly)
{
    // Idle RPM: "41 0C 05 DC\r" → 0x05DC = 1500
    // RPM = ((5 * 256) + 220) / 4 = (1280 + 220) / 4 = 1500 / 4 = 375

    std::string elmResponse = "41 0C 05 DC\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 4);

    uint8_t msb = (*binaryData)[2];  // 0x05 = 5
    uint8_t lsb = (*binaryData)[3];  // 0xDC = 220

    double rpm = obd2WordRPM(msb, lsb);
    EXPECT_EQ(rpm, 375.0);
}

TEST(OBD2EndToEnd, RPM_HighValue_CalculatesCorrectly)
{
    // High RPM: "41 0C FF FF\r" → max 16-bit value
    // RPM = ((255 * 256) + 255) / 4 = 65535 / 4 = 16383.75

    std::string elmResponse = "41 0C FF FF\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 4);

    uint8_t msb = (*binaryData)[2];  // 0xFF = 255
    uint8_t lsb = (*binaryData)[3];  // 0xFF = 255

    double rpm = obd2WordRPM(msb, lsb);
    EXPECT_EQ(rpm, 16383.75);
}

// ================================================
// Test Suite 2: Speed Signal Extraction End-to-End
// ================================================

TEST(OBD2EndToEnd, Speed_ASCII_To_VehicleSignal_CalculatesCorrectly)
{
    // Flow: Query PID 0x0D → ELM327 returns "41 0D 64\r"
    // SAE J1979: Speed = A (km/h)
    // 0x64 = 100 km/h

    std::string elmResponse = "41 0D 64\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    EXPECT_EQ((*binaryData)[0], 0x41);  // Mode 01 response
    EXPECT_EQ((*binaryData)[1], 0x0D);  // PID 0x0D (Speed)

    uint8_t speedByte = (*binaryData)[2];  // 0x64 = 100
    double speed = obd2RawValue(speedByte);

    EXPECT_EQ(speed, 100.0);
}

TEST(OBD2EndToEnd, Speed_Zero_CalculatesCorrectly)
{
    // Stopped: "41 0D 00\r" → 0 km/h

    std::string elmResponse = "41 0D 00\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t speedByte = (*binaryData)[2];  // 0x00 = 0
    double speed = obd2RawValue(speedByte);

    EXPECT_EQ(speed, 0.0);
}

TEST(OBD2EndToEnd, Speed_Maximum_CalculatesCorrectly)
{
    // Max speed: "41 0D FF\r" → 255 km/h (max single-byte value)

    std::string elmResponse = "41 0D FF\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t speedByte = (*binaryData)[2];  // 0xFF = 255
    double speed = obd2RawValue(speedByte);

    EXPECT_EQ(speed, 255.0);
}

// ================================================
// Test Suite 3: Throttle Signal Extraction End-to-End
// ================================================

TEST(OBD2EndToEnd, Throttle_ASCII_To_VehicleSignal_CalculatesCorrectly)
{
    // Flow: Query PID 0x11 → ELM327 returns "41 11 3C\r"
    // SAE J1979: Throttle % = (A / 255) * 100
    // 0x3C = 60
    // Throttle = (60 / 255) * 100 = 23.5294...%

    std::string elmResponse = "41 11 3C\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    EXPECT_EQ((*binaryData)[0], 0x41);  // Mode 01 response
    EXPECT_EQ((*binaryData)[1], 0x11);  // PID 0x11 (Throttle)

    uint8_t throttleByte = (*binaryData)[2];  // 0x3C = 60
    double throttle = obd2BytePercent(throttleByte);

    EXPECT_NEAR(throttle, 23.53, 0.1);
}

TEST(OBD2EndToEnd, Throttle_Zero_CalculatesCorrectly)
{
    // Throttle closed: "41 11 00\r" → 0%

    std::string elmResponse = "41 11 00\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t throttleByte = (*binaryData)[2];  // 0x00 = 0
    double throttle = obd2BytePercent(throttleByte);

    EXPECT_EQ(throttle, 0.0);
}

TEST(OBD2EndToEnd, Throttle_Full_CalculatesCorrectly)
{
    // Throttle wide open: "41 11 FF\r" → 100%

    std::string elmResponse = "41 11 FF\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t throttleByte = (*binaryData)[2];  // 0xFF = 255
    double throttle = obd2BytePercent(throttleByte);

    EXPECT_EQ(throttle, 100.0);
}

TEST(OBD2EndToEnd, Throttle_Half_CalculatesCorrectly)
{
    // Half throttle: "41 11 80\r" → 50%

    std::string elmResponse = "41 11 80\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t throttleByte = (*binaryData)[2];  // 0x80 = 128
    double throttle = obd2BytePercent(throttleByte);

    EXPECT_NEAR(throttle, 50.2, 0.1);
}

// ================================================
// Test Suite 4: Engine Load Signal Extraction End-to-End
// ================================================

TEST(OBD2EndToEnd, EngineLoad_ASCII_To_VehicleSignal_CalculatesCorrectly)
{
    // Flow: Query PID 0x04 → ELM327 returns "41 04 40\r"
    // SAE J1979: Engine Load % = (A / 255) * 100
    // 0x40 = 64
    // Load = (64 / 255) * 100 = 25.098...%

    std::string elmResponse = "41 04 40\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    EXPECT_EQ((*binaryData)[0], 0x41);  // Mode 01 response
    EXPECT_EQ((*binaryData)[1], 0x04);  // PID 0x04 (Engine Load)

    uint8_t loadByte = (*binaryData)[2];  // 0x40 = 64
    double load = obd2BytePercent(loadByte);

    EXPECT_NEAR(load, 25.1, 0.1);
}

// ================================================
// Test Suite 5: Temperature Signal Extraction End-to-End
// ================================================

TEST(OBD2EndToEnd, CoolantTemp_ASCII_To_VehicleSignal_CalculatesCorrectly)
{
    // Flow: Query PID 0x05 → ELM327 returns "41 05 68\r"
    // SAE J1979: Temperature = A - 40 (Celsius)
    // 0x68 = 104
    // Temp = 104 - 40 = 64°C

    std::string elmResponse = "41 05 68\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    EXPECT_EQ((*binaryData)[0], 0x41);  // Mode 01 response
    EXPECT_EQ((*binaryData)[1], 0x05);  // PID 0x05 (Coolant Temp)

    uint8_t tempByte = (*binaryData)[2];  // 0x68 = 104
    double temp = obd2TempCelsius(tempByte);

    EXPECT_EQ(temp, 64.0);
}

TEST(OBD2EndToEnd, CoolantTemp_BelowFreezing_CalculatesCorrectly)
{
    // Cold engine: "41 05 20\r"
    // 0x20 = 32
    // Temp = 32 - 40 = -8°C

    std::string elmResponse = "41 05 20\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t tempByte = (*binaryData)[2];  // 0x20 = 32
    double temp = obd2TempCelsius(tempByte);

    EXPECT_EQ(temp, -8.0);
}

TEST(OBD2EndToEnd, CoolantTemp_Minimum_CalculatesCorrectly)
{
    // Minimum: "41 05 00\r"
    // 0x00 = 0
    // Temp = 0 - 40 = -40°C (lowest measurable temp)

    std::string elmResponse = "41 05 00\r";
    auto binaryData = ELM327Transport::parseOBD2Response(elmResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 3);

    uint8_t tempByte = (*binaryData)[2];  // 0x00 = 0
    double temp = obd2TempCelsius(tempByte);

    EXPECT_EQ(temp, -40.0);
}

// ================================================
// Test Suite 6: Query Building End-to-End
// ================================================

TEST(OBD2EndToEnd, QueryRPM_BuildsCorrectASCII)
{
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x0C);
    EXPECT_EQ(query, "01 0C\r");
}

TEST(OBD2EndToEnd, QuerySpeed_BuildsCorrectASCII)
{
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x0D);
    EXPECT_EQ(query, "01 0D\r");
}

TEST(OBD2EndToEnd, QueryThrottle_BuildsCorrectASCII)
{
    std::string query = ELM327Transport::buildOBD2Query(0x01, 0x11);
    EXPECT_EQ(query, "01 11\r");
}

// ================================================
// Test Suite 7: Full Signal Extraction Scenario
// ================================================

TEST(OBD2EndToEnd, FullDrivingScenario_ExtractsAllSignals)
{
    // Simulate reading all three PIDs during driving

    // RPM: 2500 RPM
    std::string rpmResponse = "41 0C 27 10\r";
    auto rpmBinary = ELM327Transport::parseOBD2Response(rpmResponse);
    ASSERT_TRUE(rpmBinary.has_value());
    double rpm = obd2WordRPM((*rpmBinary)[2], (*rpmBinary)[3]);

    // Speed: 80 km/h
    std::string speedResponse = "41 0D 50\r";
    auto speedBinary = ELM327Transport::parseOBD2Response(speedResponse);
    ASSERT_TRUE(speedBinary.has_value());
    double speed = obd2RawValue((*speedBinary)[2]);

    // Throttle: 35%
    std::string throttleResponse = "41 11 5A\r";
    auto throttleBinary = ELM327Transport::parseOBD2Response(throttleResponse);
    ASSERT_TRUE(throttleBinary.has_value());
    double throttle = obd2BytePercent((*throttleBinary)[2]);

    // Verify all values
    EXPECT_EQ(rpm, 2500.0);
    EXPECT_EQ(speed, 80.0);
    EXPECT_NEAR(throttle, 35.3, 0.1);
}

// ================================================
// Test Suite 8: Error Handling End-to-End
// ================================================

TEST(OBD2EndToEnd, NoDataResponse_ReturnsNoSignal)
{
    std::string noDataResponse = "NO DATA\r";
    auto binaryData = ELM327Transport::parseOBD2Response(noDataResponse);

    EXPECT_FALSE(binaryData.has_value());
}

TEST(OBD2EndToEnd, SearchingPrefix_ExtractsValidData)
{
    std::string searchingResponse = "SEARCHING...\r41 0C 1A F8\r";
    auto binaryData = ELM327Transport::parseOBD2Response(searchingResponse);

    ASSERT_TRUE(binaryData.has_value());
    ASSERT_GE(binaryData->size(), 4);

    double rpm = obd2WordRPM((*binaryData)[2], (*binaryData)[3]);
    EXPECT_EQ(rpm, 1726.0);
}
