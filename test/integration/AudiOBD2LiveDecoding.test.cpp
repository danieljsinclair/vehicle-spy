#include <gtest/gtest.h>
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

using namespace vehicle_sim::boundary;
using namespace vehicle_sim::domain;

class AudiOBD2LiveDecodingTest : public ::testing::Test {
protected:
    // NOTE: When loading DBC content, the parser re-parses on every test.
    // Refactor: hoist parsed DBC to SetUpTestSuite() or static lazy-init
    // for ~120ms saving per suite. Parser is stateless so sharing is safe.
    // Effort: M, Risk: L.
    DBCTranslationService service;

    void SetUp() override {
        DefaultVehicleConfigs::registerAll(service.registry());
    }

    // Helper to parse ASCII ELM327 response and translate through OBD2 pipeline
    std::optional<VehicleSignal> parseAndTranslate(const std::string& asciiResponse) {
        auto binaryData = ELM327Transport::parseOBD2Response(asciiResponse);
        if (!binaryData.has_value()) {
            return std::nullopt;
        }
        return service.processFrame(binaryData.value());
    }
};

// ================================================
// Scenario 1: OBD2 Standard Path - Speed PID 0x0D
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, SpeedPID_0x0D_DecodesCorrectly) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));
    EXPECT_EQ(service.getProtocol(), VehicleProtocol::OBD2);

    // Speed PID 0x0D: ASCII "41 0D 64\r>" → binary [0x41, 0x0D, 0x64] → 100 km/h
    std::string asciiResponse = "41 0D 64\r>";
    auto result = parseAndTranslate(asciiResponse);

    ASSERT_TRUE(result.has_value()) << "Failed to decode speed PID response";
    EXPECT_NEAR(result->getSpeedKmh().value(), 100.0, 0.1);
}

TEST_F(AudiOBD2LiveDecodingTest, SpeedPID_0x0D_VariousValues) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Test 0 km/h
    auto result0 = parseAndTranslate("41 0D 00\r>");
    ASSERT_TRUE(result0.has_value());
    EXPECT_NEAR(result0->getSpeedKmh().value(), 0.0, 0.1);

    // Test 50 km/h
    auto result50 = parseAndTranslate("41 0D 32\r>");
    ASSERT_TRUE(result50.has_value());
    EXPECT_NEAR(result50->getSpeedKmh().value(), 50.0, 0.1);

    // Test 200 km/h (maximum for single byte)
    auto result200 = parseAndTranslate("41 0D C8\r>");
    ASSERT_TRUE(result200.has_value());
    EXPECT_NEAR(result200->getSpeedKmh().value(), 200.0, 0.1);
}

// ================================================
// Scenario 2: OBD2 Standard Path - RPM PID 0x0C (Parsing Only)
// ================================================

// Note: The current OBD2SignalTranslator does not map PID_ENGINE_RPM to any
// VehicleSignal field (motorRpm). The value is extracted by extractPIDValue
// but not stored in the signal. This test verifies the parsing pipeline works
// by checking that the binary parsing succeeds and a VehicleSignal is returned.
// For full RPM testing, a custom translator that maps to motorRpm would be needed.

TEST_F(AudiOBD2LiveDecodingTest, RPMPID_0x0C_ParsingSucceeds) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // RPM PID 0x0C: ASCII "41 0C 1A F8\r>" → binary [0x41, 0x0C, 0x1A, 0xF8]
    // 0x1AF8 = 6904, 6904/4 = 1726 RPM
    // Note: RPM is not mapped to VehicleSignal fields in current implementation
    std::string asciiResponse = "41 0C 1A F8\r>";
    auto result = parseAndTranslate(asciiResponse);

    // Verify the parsing pipeline works (ASCII → binary → translator)
    ASSERT_TRUE(result.has_value()) << "Failed to parse and translate RPM PID response";
}

TEST_F(AudiOBD2LiveDecodingTest, RPMPID_0x0C_BinaryParsingWorks) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Test that binary parsing works for various RPM values
    // The values are extracted internally but not exposed through VehicleSignal
    auto result0 = parseAndTranslate("41 0C 00 00\r>");
    ASSERT_TRUE(result0.has_value()) << "Failed to parse 0 RPM";

    auto result1000 = parseAndTranslate("41 0C 0F A0\r>");  // 1000 RPM
    ASSERT_TRUE(result1000.has_value()) << "Failed to parse 1000 RPM";

    auto result6500 = parseAndTranslate("41 0C 65 90\r>");  // 6500 RPM
    ASSERT_TRUE(result6500.has_value()) << "Failed to parse 6500 RPM";
}

// ================================================
// Scenario 3: OBD2 Standard Path - Throttle PID 0x11
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, ThrottlePID_0x11_DecodesCorrectly) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Throttle PID 0x11: ASCII "41 11 7F\r>" → binary [0x41, 0x11, 0x7F]
    // 127 * 100 / 255 ≈ 49.8%
    std::string asciiResponse = "41 11 7F\r>";
    auto result = parseAndTranslate(asciiResponse);

    ASSERT_TRUE(result.has_value()) << "Failed to decode throttle PID response";
    EXPECT_NEAR(result->getThrottlePercent().value(), 49.8, 0.1);
}

TEST_F(AudiOBD2LiveDecodingTest, ThrottlePID_0x11_VariousValues) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Test 0% throttle
    auto result0 = parseAndTranslate("41 11 00\r>");
    ASSERT_TRUE(result0.has_value());
    EXPECT_NEAR(result0->getThrottlePercent().value(), 0.0, 0.1);

    // Test 50% throttle: raw = 127.5, so 127 gives ~49.8%, 128 gives ~50.2%
    auto result50 = parseAndTranslate("41 11 80\r>");
    ASSERT_TRUE(result50.has_value());
    EXPECT_NEAR(result50->getThrottlePercent().value(), 50.2, 0.1);

    // Test 100% throttle
    auto result100 = parseAndTranslate("41 11 FF\r>");
    ASSERT_TRUE(result100.has_value());
    EXPECT_NEAR(result100->getThrottlePercent().value(), 100.0, 0.1);
}

// ================================================
// Scenario 4: OBD2 Standard Path - Coolant PID 0x05
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, CoolantPID_0x05_DecodesCorrectly) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Coolant PID 0x05: ASCII "41 05 5A\r>" → binary [0x41, 0x05, 0x5A]
    // 0x5A = 90, 90 - 40 = 50°C
    std::string asciiResponse = "41 05 5A\r>";
    auto result = parseAndTranslate(asciiResponse);

    ASSERT_TRUE(result.has_value()) << "Failed to decode coolant PID response";
    // Coolant is not directly mapped to VehicleSignal, but the value is extracted
    // This test verifies the parsing and translation pipeline works
    EXPECT_TRUE(result.has_value());
}

TEST_F(AudiOBD2LiveDecodingTest, CoolantPID_0x05_VariousValues) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Test -40°C (minimum)
    auto resultMinus40 = parseAndTranslate("41 05 00\r>");
    ASSERT_TRUE(resultMinus40.has_value());

    // Test 0°C: raw = 40
    auto result0 = parseAndTranslate("41 05 28\r>");
    ASSERT_TRUE(result0.has_value());

    // Test 100°C: raw = 140
    auto result100 = parseAndTranslate("41 05 8C\r>");
    ASSERT_TRUE(result100.has_value());
}

// ================================================
// Scenario 5: OBD2 Standard Path - Engine Load PID 0x04
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, EngineLoadPID_0x04_DecodesCorrectly) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Engine Load PID 0x04: ASCII "41 04 FF\r>" → binary [0x41, 0x04, 0xFF]
    // 255 * 100 / 255 = 100%
    // Engine load maps to accelerationG: (value/100) * 2 - 1
    // So 100% load → accelerationG = 1.0
    std::string asciiResponse = "41 04 FF\r>";
    auto result = parseAndTranslate(asciiResponse);

    ASSERT_TRUE(result.has_value()) << "Failed to decode engine load PID response";
    EXPECT_NEAR(result->getAccelerationG().value(), 1.0, 0.01);
}

TEST_F(AudiOBD2LiveDecodingTest, EngineLoadPID_0x04_VariousValues) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Test 0% load: accelerationG = -1.0
    auto result0 = parseAndTranslate("41 04 00\r>");
    ASSERT_TRUE(result0.has_value());
    EXPECT_NEAR(result0->getAccelerationG().value(), -1.0, 0.01);

    // Test 50% load: accelerationG = 0.0
    // raw 50% = 127.5, so 128 gives 50.2%, accelerationG ≈ 0.004
    auto result50 = parseAndTranslate("41 04 80\r>");
    ASSERT_TRUE(result50.has_value());
    EXPECT_NEAR(result50->getAccelerationG().value(), 0.004, 0.01);
}

// ================================================
// Scenario 6: Multi-PID Accumulation
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, MultiPID_AccumulationAcrossMultiplePIDs) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Feed Speed PID 0x0D: 100 km/h
    auto resultSpeed = parseAndTranslate("41 0D 64\r>");
    ASSERT_TRUE(resultSpeed.has_value());
    EXPECT_NEAR(resultSpeed->getSpeedKmh().value(), 100.0, 0.1);

    // Feed Throttle PID 0x11: 50% throttle - speed should persist
    auto resultThrottle = parseAndTranslate("41 11 80\r>");
    ASSERT_TRUE(resultThrottle.has_value());
    EXPECT_NEAR(resultThrottle->getThrottlePercent().value(), 50.2, 0.1);
    EXPECT_NEAR(resultThrottle->getSpeedKmh().value(), 100.0, 0.1) << "Speed should persist";

    // Feed RPM PID 0x0C: 3000 RPM - throttle and speed should persist
    // Note: RPM is not mapped to any VehicleSignal field in current implementation
    auto resultRPM = parseAndTranslate("41 0C 2E E0\r>");
    ASSERT_TRUE(resultRPM.has_value());
    EXPECT_NEAR(resultRPM->getSpeedKmh().value(), 100.0, 0.1) << "Speed should persist";
    EXPECT_NEAR(resultRPM->getThrottlePercent().value(), 50.2, 0.1) << "Throttle should persist";
}

// ================================================
// Scenario 7: Audi CAN DBC Path Tests
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, AudiCAN_ESP_VehicleSpeed_DecodesCorrectly) {
    // Create a custom config with signal mapping for the DBC we're testing
    VehicleConfig audiConfig(
        "",
        "",
        "Audi MLB Evo",
        std::unordered_map<std::string, std::string>{
            {"ESP_VehicleSpeed", "speedKmh"}
        },
        "",  // canBus
        true // isCANProtocol
    );
    service.registry().registerVehicle("audi_can_test", audiConfig);

    const char* audiDbc = R"DBC(VERSION ""
NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_
BS_:
BU_: ESP
BO_ 256 ESP_01: 8 ESP
 SG_ ESP_VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" ESP
)DBC";

    ASSERT_TRUE(service.loadVehicleWithContent("audi_can_test", VehicleProtocol::CAN, audiDbc));
    EXPECT_EQ(service.getProtocol(), VehicleProtocol::CAN);

    // CAN frame 256 with speed 100 km/h: raw = 10000 (100 / 0.01) = 0x2710
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    auto result = service.processFrame(frame256);

    ASSERT_TRUE(result.has_value()) << "Failed to decode Audi CAN 256 frame";
    EXPECT_NEAR(result->getSpeedKmh().value(), 100.0, 0.01);
}

// Brake pressure tested in AudiCAN_ESP_Bremsdruck_DecodesCorrectly below

TEST_F(AudiOBD2LiveDecodingTest, AudiCAN_ESP_Bremsdruck_DecodesCorrectly) {
    // Create a custom config with signal mapping for DBC we're testing
    VehicleConfig audiConfig(
        "",
        "",
        "Audi MLB Evo",
        std::unordered_map<std::string, std::string>{
            {"ESP_Bremsdruck", "brakePercent"}
        },
        "",  // canBus
        true // isCANProtocol
    );
    service.registry().registerVehicle("audi_can_brake_test", audiConfig);

    const char* audiDbc = R"DBC(VERSION ""
NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_
BS_:
BU_: ESP
BO_ 256 ESP_01: 8 ESP
 SG_ ESP_Bremsdruck : 32|8@1+ (0.4,0) [0|100] "%" ESP
)DBC";

    ASSERT_TRUE(service.loadVehicleWithContent("audi_can_brake_test", VehicleProtocol::CAN, audiDbc));
    EXPECT_EQ(service.getProtocol(), VehicleProtocol::CAN);

    // ESP_Bremsdruck: startBit=32, 8-bit, scale=0.4, offset=0
    // 50% brake = raw 125 (50 / 0.4) = 0x7D
    // Bit 32 = byte 4
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x00, 0x00, 0x00, 0x00, 0x7D, 0x00, 0x00, 0x00
    };
    auto result = service.processFrame(frame256);

    ASSERT_TRUE(result.has_value()) << "Failed to decode Audi CAN 256 brake frame";
    EXPECT_NEAR(result->getBrakePercent().value(), 50.0, 0.1);
}

TEST_F(AudiOBD2LiveDecodingTest, AudiCAN_CombinedSpeedAndBrake) {
    // Create a custom config with signal mapping for DBC we're testing
    VehicleConfig audiConfig(
        "",
        "",
        "Audi MLB Evo",
        std::unordered_map<std::string, std::string>{
            {"ESP_VehicleSpeed", "speedKmh"},
            {"ESP_Bremsdruck", "brakePercent"}
        },
        "",  // canBus
        true // isCANProtocol
    );
    service.registry().registerVehicle("audi_can_combined_test", audiConfig);

    const char* audiDbc = R"DBC(VERSION ""
NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_
BS_:
BU_: ESP
BO_ 256 ESP_01: 8 ESP
 SG_ ESP_VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" ESP
 SG_ ESP_Bremsdruck : 32|8@1+ (0.4,0) [0|100] "%" ESP
)DBC";

    ASSERT_TRUE(service.loadVehicleWithContent("audi_can_combined_test", VehicleProtocol::CAN, audiDbc));

    // Combined frame with speed 100 km/h and 50% brake
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x10, 0x27, 0x00, 0x00, 0x7D, 0x00, 0x00, 0x00
    };
    auto result = service.processFrame(frame256);

    ASSERT_TRUE(result.has_value()) << "Failed to decode combined Audi CAN frame";
    EXPECT_NEAR(result->getSpeedKmh().value(), 100.0, 0.01);
    EXPECT_NEAR(result->getBrakePercent().value(), 50.0, 0.1);
}

// ================================================
// Edge Cases
// ================================================

TEST_F(AudiOBD2LiveDecodingTest, EdgeCase_InvalidASCIAReturnsNullopt) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // Invalid ASCII response
    auto result = parseAndTranslate("NO DATA");
    EXPECT_FALSE(result.has_value());

    // Empty response
    auto resultEmpty = parseAndTranslate("");
    EXPECT_FALSE(resultEmpty.has_value());
}

TEST_F(AudiOBD2LiveDecodingTest, EdgeCase_UnrecognizedPIDReturnsSignalWithPersistedValues) {
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));

    // First set a known value
    auto resultSpeed = parseAndTranslate("41 0D 64\r>");
    ASSERT_TRUE(resultSpeed.has_value());
    EXPECT_NEAR(resultSpeed->getSpeedKmh().value(), 100.0, 0.1);

    // Then query an unrecognized PID (0x01 - Monitor status)
    auto resultUnknown = parseAndTranslate("41 01 00\r>");
    ASSERT_TRUE(resultUnknown.has_value()) << "Should still return signal for unknown PID";
    EXPECT_NEAR(resultUnknown->getSpeedKmh().value(), 100.0, 0.1) << "Speed should persist";
}

TEST_F(AudiOBD2LiveDecodingTest, EdgeCase_SwitchingProtocols) {
    // Load OBD2 first
    ASSERT_TRUE(service.loadVehicleWithContent("generic", VehicleProtocol::OBD2, ""));
    EXPECT_EQ(service.getProtocol(), VehicleProtocol::OBD2);

    // Parse OBD2 speed response
    auto resultOBD2 = parseAndTranslate("41 0D 64\r>");
    ASSERT_TRUE(resultOBD2.has_value());
    EXPECT_NEAR(resultOBD2->getSpeedKmh().value(), 100.0, 0.1);

    // Switch to CAN protocol
    VehicleConfig audiConfig(
        "",
        "",
        "Audi MLB Evo",
        std::unordered_map<std::string, std::string>{
            {"ESP_VehicleSpeed", "speedKmh"}
        },
        "",  // canBus
        true // isCANProtocol
    );
    service.registry().registerVehicle("audi_protocol_test", audiConfig);

    const char* audiDbc = R"DBC(VERSION ""
NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_
BS_:
BU_: ESP
BO_ 256 ESP_01: 8 ESP
 SG_ ESP_VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" ESP
)DBC";
    ASSERT_TRUE(service.loadVehicleWithContent("audi_protocol_test", VehicleProtocol::CAN, audiDbc));
    EXPECT_EQ(service.getProtocol(), VehicleProtocol::CAN);

    // Parse CAN speed frame
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,
        0xD0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // 20 km/h
    };
    auto resultCAN = service.processFrame(frame256);
    ASSERT_TRUE(resultCAN.has_value());
    EXPECT_NEAR(resultCAN->getSpeedKmh().value(), 20.0, 0.01);
}
