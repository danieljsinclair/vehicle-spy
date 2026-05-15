#include <gtest/gtest.h>
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

class DBCLiveDecodingTest : public ::testing::Test {
protected:
    DBCFileParser parser;

    // Helper to create Tesla config
    VehicleConfig createTeslaConfig() {
        return VehicleConfig(
            "embedded_tesla.dbc",
            "Tesla Model 3",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DIR_torqueActual", "motorTorqueNm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"SteeringAngle129", "steeringAngleDeg"}
            },
            "",  // canBus
            true // isCANProtocol
        );
    }

    // Helper to create Audi config
    VehicleConfig createAudiConfig() {
        return VehicleConfig(
            "embedded_audi.dbc",
            "Audi MLB Evo",
            std::unordered_map<std::string, std::string>{
                {"ESP_v_Signal", "speedKmh"},
                {"ESP_Laengsbeschl", "accelerationG"},
                {"ESP_Bremsdruck", "brakePercent"}
            },
            "",  // canBus
            true // isCANProtocol
        );
    }

    // Embedded Tesla Model 3 DBC content
    // Based on actual Model3CAN.dbc structure with relevant signals
    const char* getTeslaDBC() const {
        return R"DBC(VERSION ""

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

BU_: DIR DI SCCM

BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
 SG_ DIR_torqueActual : 27|13@1- (2,0) [-7500|7500] "Nm" DIR

BO_ 280 DI_state: 8 DI
 SG_ DI_accelPedalPos : 32|8@1+ (0.4,0) [0|100] "%" DI
 SG_ DI_brakePedalState : 19|2@1+ (1,0) [0|2] "" DI

BO_ 297 SCCM_steeringAngle: 5 SCCM
 SG_ SteeringAngle129 : 16|14@1+ (0.1,-819.2) [-819.2|819.1] "deg" SCCM

VAL_ 280 DI_brakePedalState 0 "released" 1 "pressed" ;
)DBC";
    }

    // Embedded Audi MLB Evo DBC content
    // Based on vw_mlb.dbc structure with relevant ESP signals
    const char* getAudiDBC() const {
        return R"DBC(VERSION ""

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
 SG_ ESP_v_Signal : 0|16@1+ (0.01,0) [0|655.35] "km/h" ESP
 SG_ ESP_Laengsbeschl : 16|16@1- (0.01,-327.68) [-327.68|327.67] "m/s^2" ESP
 SG_ ESP_Bremsdruck : 32|8@1+ (0.4,0) [0|100] "%" ESP
)DBC";
    }
};

// ================================================
// Scenario 1: Tesla Model 3 CAN Frame Decoding
// ================================================

TEST_F(DBCLiveDecodingTest, Tesla_CAN264_DecodesMotorRpmAndTorque) {
    auto parseResult = parser.parseString(getTeslaDBC());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // DIR_axleSpeed: startBit=40, 16-bit, Intel signed, scale=0.1, offset=0
    // 1000 RPM = raw 10000 (1000 / 0.1) = 0x2710
    // Data payload byte 5 = 0x10, byte 6 = 0x27 (bits 40-55)
    std::vector<std::uint8_t> frame264_rpm = {
        0x08, 0x01,  // CAN ID 264 (little-endian)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00  // axleSpeed = 1000 RPM
    };
    auto result_rpm = translator.translate(frame264_rpm);
    ASSERT_TRUE(result_rpm.has_value()) << "Failed to translate CAN 264 frame for RPM";
    EXPECT_NEAR(result_rpm->getMotorRpm().value(), 1000.0, 0.1);

    // DIR_torqueActual: startBit=27, 13-bit, Intel signed, scale=2, offset=0
    // 100 Nm → raw = 50 (100 / 2)
    // startBit=27 → data_byte[3] bit 3. 13 bits: byte3[3-7] + byte4[0-7]
    // raw 50: lower 5 bits (0b10010=18) → byte3 = 18<<3 = 0x90
    //         upper 8 bits (0b00000001=1)  → byte4 = 0x01
    std::vector<std::uint8_t> frame264_torque = {
        0x08, 0x01,
        0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x00
    };
    auto result_torque = translator.translate(frame264_torque);
    ASSERT_TRUE(result_torque.has_value()) << "Failed to translate CAN 264 frame for torque";
    EXPECT_NEAR(result_torque->getMotorTorqueNm().value(), 100.0, 0.5);
}

TEST_F(DBCLiveDecodingTest, Tesla_CAN280_DecodesThrottlePercent) {
    auto parseResult = parser.parseString(getTeslaDBC());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // DI_accelPedalPos: startBit=32, 8-bit, Intel unsigned, scale=0.4, offset=0
    // 75.2% throttle = raw 188 (75.2 / 0.4) = 0xBC
    // Bit 32 = byte 4 of data payload
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00  // throttle = 75.2%
    };
    auto result = translator.translate(frame280);
    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 280 frame";
    EXPECT_NEAR(result->getThrottlePercent().value(), 75.2, 0.1);
}

TEST_F(DBCLiveDecodingTest, Tesla_CAN297_DecodesSteeringAngle) {
    auto parseResult = parser.parseString(getTeslaDBC());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // SteeringAngle129: startBit=16, 14-bit, Intel unsigned, scale=0.1, offset=-819.2
    // 0 deg center = raw 8192 = 0x2000
    // Bit 16-29: byte 2-3 of data payload
    std::vector<std::uint8_t> frame297_center = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00  // steering = 0.0 deg
    };
    auto result_center = translator.translate(frame297_center);
    ASSERT_TRUE(result_center.has_value()) << "Failed to translate CAN 297 frame (center)";
    EXPECT_NEAR(result_center->getSteeringAngleDeg().value(), 0.0, 0.1);

    // 45 deg right = raw 8642 (45 / 0.1 + 8192) = 0x21C2
    std::vector<std::uint8_t> frame297_right = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0xC2, 0x21, 0x00, 0x00, 0x00, 0x00  // steering = 45.0 deg
    };
    auto result_right = translator.translate(frame297_right);
    ASSERT_TRUE(result_right.has_value()) << "Failed to translate CAN 297 frame (right)";
    EXPECT_NEAR(result_right->getSteeringAngleDeg().value(), 45.0, 0.1);

    // -30 deg left = raw 7892 (-30 / 0.1 + 8192) = 0x1ED4
    std::vector<std::uint8_t> frame297_left = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0xD4, 0x1E, 0x00, 0x00, 0x00, 0x00  // steering = -30.0 deg
    };
    auto result_left = translator.translate(frame297_left);
    ASSERT_TRUE(result_left.has_value()) << "Failed to translate CAN 297 frame (left)";
    EXPECT_NEAR(result_left->getSteeringAngleDeg().value(), -30.0, 0.1);
}

TEST_F(DBCLiveDecodingTest, Tesla_MultiFrameAccumulation) {
    auto parseResult = parser.parseString(getTeslaDBC());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed CAN 264 - sets motorRpm and motorTorqueNm
    // torque: byte3=0x90, byte4=0x01. RPM: byte5=0x10, byte6=0x27
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,
        0x00, 0x00, 0x00, 0x90, 0x01, 0x10, 0x27, 0x00
    };
    auto r1 = translator.translate(frame264);
    ASSERT_TRUE(r1.has_value());
    EXPECT_NEAR(r1->getMotorTorqueNm().value(), 100.0, 0.5);
    EXPECT_NEAR(r1->getMotorRpm().value(), 1000.0, 0.1);

    // Feed CAN 280 - sets throttlePercent, preserves motorRpm/torque
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00  // 75.2% throttle
    };
    auto r2 = translator.translate(frame280);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getThrottlePercent().value(), 75.2, 0.1);
    EXPECT_NEAR(r2->getMotorTorqueNm().value(), 100.0, 0.5) << "Torque should persist";
    EXPECT_NEAR(r2->getMotorRpm().value(), 1000.0, 0.1) << "RPM should persist";

    // Feed CAN 297 - sets steeringAngleDeg, all fields present
    std::vector<std::uint8_t> frame297 = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00  // 0 deg steering
    };
    auto r3 = translator.translate(frame297);
    ASSERT_TRUE(r3.has_value());
    EXPECT_NEAR(r3->getSteeringAngleDeg().value(), 0.0, 0.1);
    EXPECT_NEAR(r3->getMotorTorqueNm().value(), 100.0, 0.5) << "Torque should persist";
    EXPECT_NEAR(r3->getMotorRpm().value(), 1000.0, 0.1) << "RPM should persist";
    EXPECT_NEAR(r3->getThrottlePercent().value(), 75.2, 0.1) << "Throttle should persist";
}

// ================================================
// Scenario 2: Audi MLB Evo CAN Frame Decoding
// ================================================

TEST_F(DBCLiveDecodingTest, Audi_CAN256_DecodesSpeedAndBrake) {
    auto parseResult = parser.parseString(getAudiDBC());
    auto config = createAudiConfig();
    DBCSignalTranslator translator(config, parseResult);

    // ESP_v_Signal: startBit=0, 16-bit, Intel unsigned, scale=0.01, offset=0
    // 100 km/h = raw 10000 (100 / 0.01) = 0x2710
    // Bit 0 = byte 0
    std::vector<std::uint8_t> frame256_speed = {
        0x00, 0x01,  // CAN ID 256
        0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // speed = 100 km/h
    };
    auto result_speed = translator.translate(frame256_speed);
    ASSERT_TRUE(result_speed.has_value()) << "Failed to translate CAN 256 frame for speed";
    EXPECT_NEAR(result_speed->getSpeedKmh().value(), 100.0, 0.01);

    // ESP_Bremsdruck: startBit=32, 8-bit, Intel unsigned, scale=0.4, offset=0
    // 50% brake = raw 125 (50 / 0.4) = 0x7D
    // Bit 32 = byte 4
    std::vector<std::uint8_t> frame256_brake = {
        0x00, 0x01,  // CAN ID 256
        0x00, 0x00, 0x00, 0x00, 0x7D, 0x00, 0x00, 0x00  // brake = 50%
    };
    auto result_brake = translator.translate(frame256_brake);
    ASSERT_TRUE(result_brake.has_value()) << "Failed to translate CAN 256 frame for brake";
    EXPECT_NEAR(result_brake->getBrakePercent().value(), 50.0, 0.1);

    // Combined frame with speed and brake
    std::vector<std::uint8_t> frame256_combined = {
        0x00, 0x01,  // CAN ID 256
        0x10, 0x27, 0x00, 0x00, 0x7D, 0x00, 0x00, 0x00  // 100 km/h, 50% brake
    };
    auto result_combined = translator.translate(frame256_combined);
    ASSERT_TRUE(result_combined.has_value()) << "Failed to translate CAN 256 frame for combined";
    EXPECT_NEAR(result_combined->getSpeedKmh().value(), 100.0, 0.01);
    EXPECT_NEAR(result_combined->getBrakePercent().value(), 50.0, 0.1);
}

TEST_F(DBCLiveDecodingTest, Audi_CAN256_DecodesBrakePercent) {
    auto parseResult = parser.parseString(getAudiDBC());
    auto config = createAudiConfig();
    DBCSignalTranslator translator(config, parseResult);

    // ESP_Bremsdruck: startBit=32, 8-bit, Intel unsigned, scale=0.4, offset=0
    // 50% brake = raw 125 (50 / 0.4) = 0x7D
    // Bit 32 = byte 4
    std::vector<std::uint8_t> frame256_brake = {
        0x00, 0x01,  // CAN ID 256
        0x00, 0x00, 0x00, 0x00, 0x7D, 0x00, 0x00, 0x00  // brake = 50%
    };
    auto result = translator.translate(frame256_brake);
    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 256 frame for brake";
    EXPECT_NEAR(result->getBrakePercent().value(), 50.0, 0.1);
}

TEST_F(DBCLiveDecodingTest, Audi_MultiFrameAccumulation) {
    auto parseResult = parser.parseString(getAudiDBC());
    auto config = createAudiConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed CAN 256 - sets speedKmh, accelerationG, and brakePercent
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x10, 0x27, 0x00, 0x80, 0x7D, 0x00, 0x00, 0x00  // 100 km/h, 0g accel, 50% brake
    };
    auto r1 = translator.translate(frame256);
    ASSERT_TRUE(r1.has_value());
    EXPECT_NEAR(r1->getSpeedKmh().value(), 100.0, 0.01);
    EXPECT_NEAR(r1->getBrakePercent().value(), 50.0, 0.1);

    // Feed another CAN 256 with different speed
    std::vector<std::uint8_t> frame256_2 = {
        0x00, 0x01,  // CAN ID 256
        0xD0, 0x07, 0x00, 0x80, 0x7D, 0x00, 0x00, 0x00  // 20 km/h, 0g accel, 50% brake
    };
    auto r2 = translator.translate(frame256_2);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getSpeedKmh().value(), 20.0, 0.01);
    EXPECT_NEAR(r2->getBrakePercent().value(), 50.0, 0.1);
}

// ================================================
// Scenario 3: Cross-vehicle validation
// ================================================

TEST_F(DBCLiveDecodingTest, CrossVehicle_TeslaConfigDoesNotDecodeAudiSignals) {
    // Parse Tesla DBC
    auto teslaParseResult = parser.parseString(getTeslaDBC());
    auto teslaConfig = createTeslaConfig();
    DBCSignalTranslator teslaTranslator(teslaConfig, teslaParseResult);

    // Feed Audi CAN 256 frame (not in Tesla DBC)
    std::vector<std::uint8_t> audiFrame = {
        0x00, 0x01,  // CAN ID 256 (Audi)
        0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // speed data
    };
    auto result = teslaTranslator.translate(audiFrame);

    // Should return signal but with default/unmodified values
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(result->getSpeedKmh().has_value()) << "Audi signal should not be decoded by Tesla config";
}

TEST_F(DBCLiveDecodingTest, CrossVehicle_AudiConfigDoesNotDecodeTeslaSignals) {
    // Parse Audi DBC
    auto audiParseResult = parser.parseString(getAudiDBC());
    auto audiConfig = createAudiConfig();
    DBCSignalTranslator audiTranslator(audiConfig, audiParseResult);

    // Feed Tesla CAN 264 frame (not in Audi DBC)
    std::vector<std::uint8_t> teslaFrame = {
        0x08, 0x01,  // CAN ID 264 (Tesla)
        0x00, 0x00, 0x90, 0x19, 0xA0, 0x0F, 0x00, 0x00  // motor data
    };
    auto result = audiTranslator.translate(teslaFrame);

    // Should return signal but with default/unmodified values
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(result->getMotorRpm().has_value()) << "Tesla signal should not be decoded by Audi config";
    EXPECT_FALSE(result->getMotorTorqueNm().has_value()) << "Tesla torque should not be decoded by Audi config";
}

TEST_F(DBCLiveDecodingTest, UnknownCANIdReturnsSignalWithDefaults) {
    auto teslaParseResult = parser.parseString(getTeslaDBC());
    auto teslaConfig = createTeslaConfig();
    DBCSignalTranslator translator(teslaConfig, teslaParseResult);

    // Feed frame with CAN ID 999 (not in any DBC)
    std::vector<std::uint8_t> unknownFrame = {
        0xE7, 0x03,  // CAN ID 999
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    auto result = translator.translate(unknownFrame);
    EXPECT_TRUE(result.has_value()) << "Should return signal even for unknown CAN ID";
    EXPECT_FALSE(result->getMotorRpm().has_value());
    EXPECT_FALSE(result->getThrottlePercent().has_value());
}

// ================================================
// Scenario: DBCTranslationService End-to-End
// ================================================

TEST_F(DBCLiveDecodingTest, DBCTranslationService_TeslaEndToEnd) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig = createTeslaConfig();
    registry.registerVehicle("tesla_model3", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, getTeslaDBC()));
    EXPECT_TRUE(service.isLoaded());
    EXPECT_EQ(service.getVehicleId(), "tesla_model3");

    // CAN 264 with torque and RPM
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,
        0x00, 0x00, 0x00, 0x90, 0x01, 0x10, 0x27, 0x00
    };
    auto result264 = service.processFrame(frame264);
    ASSERT_TRUE(result264.has_value()) << "Failed to process CAN 264 frame";
    EXPECT_NEAR(result264->getMotorTorqueNm().value(), 100.0, 0.5);
    EXPECT_NEAR(result264->getMotorRpm().value(), 1000.0, 0.1);

    // CAN 280 with throttle
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,
        0x00, 0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00
    };
    auto result280 = service.processFrame(frame280);
    ASSERT_TRUE(result280.has_value()) << "Failed to process CAN 280 frame";
    EXPECT_NEAR(result280->getThrottlePercent().value(), 75.2, 0.1);
}

TEST_F(DBCLiveDecodingTest, DBCTranslationService_AudiEndToEnd) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig audiConfig = createAudiConfig();
    registry.registerVehicle("audi_mlb_evo", audiConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("audi_mlb_evo", VehicleProtocol::CAN, getAudiDBC()));
    EXPECT_TRUE(service.isLoaded());
    EXPECT_EQ(service.getVehicleId(), "audi_mlb_evo");

    // CAN 256 with speed
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,
        0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    auto result = service.processFrame(frame256);
    ASSERT_TRUE(result.has_value()) << "Failed to process CAN 256 frame";
    EXPECT_NEAR(result->getSpeedKmh().value(), 100.0, 0.01);
}

// ================================================
// Edge Cases: Frame validation
// ================================================

TEST_F(DBCLiveDecodingTest, EdgeCase_FrameTooShortReturnsNullopt) {
    auto teslaParseResult = parser.parseString(getTeslaDBC());
    auto teslaConfig = createTeslaConfig();
    DBCSignalTranslator translator(teslaConfig, teslaParseResult);

    // Frame with only 9 bytes (ID + 7 data bytes)
    std::vector<std::uint8_t> shortFrame = {
        0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    auto result = translator.translate(shortFrame);
    EXPECT_FALSE(result.has_value()) << "Short frame should return nullopt";
}

TEST_F(DBCLiveDecodingTest, EdgeCase_EmptyDBCContentReturnsEmptyResult) {
    auto parseResult = parser.parseString("");
    EXPECT_EQ(parseResult.totalSignalCount(), 0);
    EXPECT_TRUE(parseResult.signalsByCanId.empty());
}

TEST_F(DBCLiveDecodingTest, EdgeCase_InvalidDBCContentReturnsEmptyResult) {
    auto parseResult = parser.parseString("This is not a valid DBC file");
    EXPECT_EQ(parseResult.totalSignalCount(), 0);
    EXPECT_TRUE(parseResult.signalsByCanId.empty());
}
