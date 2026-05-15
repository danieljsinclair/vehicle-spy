#include <gtest/gtest.h>
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleConfig.h"

using namespace vehicle_sim;
using namespace vehicle_sim::boundary;
using namespace vehicle_sim::domain;

namespace {

// Helper: convert CANFrame to DBC translator format (10-byte array)
// DBC format: [canId_lo, canId_hi, data[0], ..., data[7]]
std::vector<uint8_t> canFrameToDBC(const CANFrame& frame) {
    std::vector<uint8_t> result(10);
    result[0] = frame.canId & 0xFF;
    result[1] = (frame.canId >> 8) & 0xFF;
    std::copy(frame.data.begin(), frame.data.end(), result.begin() + 2);
    return result;
}

// Tesla Model 3 DBC content - minimal subset for testing
const std::string TESLA_DBC = R"DBC(VERSION ""

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

} // namespace

// ================================================
// Test Suite 1: ELM327 CAN Frame Parsing
// ================================================

TEST(TeslaCANLiveDecoding, ParseCANFrame264_NoPrefix) {
    // ELM327 monitor mode output for CAN ID 264 (0x108 hex)
    // Tesla CAN ID 264 decimal = 0x108 hex
    // RPM 1000: data[5]=0x10, data[6]=0x27 (raw 10000 * 0.1 = 1000 RPM)
    // Torque 100 Nm: data[3]=0x90, data[4]=0x01 (raw 50 * 2 = 100 Nm)
    auto frame = ELM327Transport::parseCANFrame("108 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->canId, 0x108); // 0x108 hex = 264 decimal
    EXPECT_EQ(frame->data.size(), 8);
    EXPECT_EQ(frame->data[0], 0x00);
    EXPECT_EQ(frame->data[1], 0x00);
    EXPECT_EQ(frame->data[2], 0x00);
    EXPECT_EQ(frame->data[3], 0x90);
    EXPECT_EQ(frame->data[4], 0x01);
    EXPECT_EQ(frame->data[5], 0x10);
    EXPECT_EQ(frame->data[6], 0x27);
    EXPECT_EQ(frame->data[7], 0x00);
}

TEST(TeslaCANLiveDecoding, ParseCANFrame264_WithTypePrefix) {
    // CAN frame with type byte (0x600 + type) for ELM327 monitor mode
    // Type 0x10 + 0x600 = 0x610, followed by CAN ID 0x108 and data
    auto frame = ELM327Transport::parseCANFrame("610 108 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->canId, 0x108);
    EXPECT_EQ(frame->data.size(), 8);
}

TEST(TeslaCANLiveDecoding, ParseCANFrame280_Throttle) {
    // Tesla CAN ID 280 decimal = 0x118 hex
    // Throttle 75.2%: data[4]=0xBC (raw 188 * 0.4 = 75.2%)
    auto frame = ELM327Transport::parseCANFrame("118 00 00 00 00 BC 00 00 00");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->canId, 0x118); // 0x118 hex = 280 decimal
    EXPECT_EQ(frame->data[4], 0xBC);
}

TEST(TeslaCANLiveDecoding, ParseCANFrame297_Steering) {
    // Tesla CAN ID 297 decimal = 0x129 hex
    // Steering 0 deg: data[3]=0x20, data[2]=0x00 (raw 8192)
    auto frame = ELM327Transport::parseCANFrame("129 00 00 00 20 00 00 00 00");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->canId, 0x129); // 0x129 hex = 297 decimal
    EXPECT_EQ(frame->data[2], 0x00);
    EXPECT_EQ(frame->data[3], 0x20);
}

// ================================================
// Test Suite 2: CANFrame to DBC Format Conversion
// ================================================

TEST(TeslaCANLiveDecoding, CANFrameToDBCFormat_CanIdEncoding) {
    // Verify CAN ID is encoded correctly as little-endian
    CANFrame frame;
    frame.canId = 0x0108; // 264 decimal
    frame.data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto dbcFormat = canFrameToDBC(frame);
    ASSERT_EQ(dbcFormat.size(), 10);
    EXPECT_EQ(dbcFormat[0], 0x08); // CAN ID low byte
    EXPECT_EQ(dbcFormat[1], 0x01); // CAN ID high byte
}

TEST(TeslaCANLiveDecoding, CANFrameToDBCFormat_DataPreserved) {
    CANFrame frame;
    frame.canId = 0x0108;
    frame.data = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

    auto dbcFormat = canFrameToDBC(frame);
    ASSERT_EQ(dbcFormat.size(), 10);
    EXPECT_EQ(dbcFormat[2], 0xAA); // data[0]
    EXPECT_EQ(dbcFormat[3], 0xBB); // data[1]
    EXPECT_EQ(dbcFormat[9], 0x22); // data[7]
}

// ================================================
// Test Suite 3: End-to-End ELM327 → DBC Translation
// ================================================

TEST(TeslaCANLiveDecoding, EndToEnd_CAN264_MotorRpmAndTorque) {
    DBCTranslationService service;
    auto& registry = service.registry();

    // Create Tesla config with correct signal mappings
    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        },
        "",
        true // isCANProtocol
    );
    registry.registerVehicle("tesla_model3", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, TESLA_DBC));

    // Parse ELM327 output for CAN 264
    auto frame = ELM327Transport::parseCANFrame("108 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(frame.has_value());

    // Convert to DBC format and translate
    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 264 frame";
    EXPECT_NEAR(result->getMotorRpm().value(), 1000.0, 0.1);
    EXPECT_NEAR(result->getMotorTorqueNm().value(), 100.0, 0.5);
}

TEST(TeslaCANLiveDecoding, EndToEnd_CAN280_Throttle) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla_model3", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, TESLA_DBC));

    // Parse ELM327 output for CAN 280
    auto frame = ELM327Transport::parseCANFrame("118 00 00 00 00 BC 00 00 00");
    ASSERT_TRUE(frame.has_value());

    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 280 frame";
    EXPECT_NEAR(result->getThrottlePercent().value(), 75.2, 0.1);
}

TEST(TeslaCANLiveDecoding, EndToEnd_CAN297_SteeringAngle) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla_model3", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, TESLA_DBC));

    // Steering 45 deg right: raw 8642 = 0x21C2
    auto frame = ELM327Transport::parseCANFrame("129 00 00 C2 21 00 00 00 00");
    ASSERT_TRUE(frame.has_value());

    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 297 frame";
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), 45.0, 0.1);
}

// ================================================
// Test Suite 4: Multi-Frame Signal Accumulation
// ================================================

TEST(TeslaCANLiveDecoding, MultiFrame_AllSignalsAccumulate) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla_model3", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, TESLA_DBC));

    // Feed CAN 264 - sets motorRpm and motorTorqueNm
    auto frame264 = ELM327Transport::parseCANFrame("108 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(frame264.has_value());
    auto r1 = service.processFrame(canFrameToDBC(*frame264));
    ASSERT_TRUE(r1.has_value());
    EXPECT_NEAR(r1->getMotorRpm().value(), 1000.0, 0.1);
    EXPECT_NEAR(r1->getMotorTorqueNm().value(), 100.0, 0.5);

    // Feed CAN 280 - sets throttlePercent, preserves other signals
    auto frame280 = ELM327Transport::parseCANFrame("118 00 00 00 00 BC 00 00 00");
    ASSERT_TRUE(frame280.has_value());
    auto r2 = service.processFrame(canFrameToDBC(*frame280));
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getThrottlePercent().value(), 75.2, 0.1);
    EXPECT_NEAR(r2->getMotorRpm().value(), 1000.0, 0.1) << "RPM should persist";
    EXPECT_NEAR(r2->getMotorTorqueNm().value(), 100.0, 0.5) << "Torque should persist";

    // Feed CAN 297 - sets steeringAngleDeg, all fields present
    auto frame297 = ELM327Transport::parseCANFrame("129 00 00 00 20 00 00 00 00");
    ASSERT_TRUE(frame297.has_value());
    auto r3 = service.processFrame(canFrameToDBC(*frame297));
    ASSERT_TRUE(r3.has_value());
    EXPECT_NEAR(r3->getSteeringAngleDeg().value(), 0.0, 0.1);
    EXPECT_NEAR(r3->getMotorRpm().value(), 1000.0, 0.1) << "RPM should persist";
    EXPECT_NEAR(r3->getMotorTorqueNm().value(), 100.0, 0.5) << "Torque should persist";
    EXPECT_NEAR(r3->getThrottlePercent().value(), 75.2, 0.1) << "Throttle should persist";
}

// ================================================
// Test Suite 5: DefaultVehicleConfigs Integration
// ================================================

TEST(TeslaCANLiveDecoding, DefaultConfig_TeslaModel3_Works) {
    DBCTranslationService service;

    // Register default Tesla config
    DefaultVehicleConfigs::registerAll(service.registry());

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, TESLA_DBC));

    // Parse and translate CAN 264 frame
    auto frame = ELM327Transport::parseCANFrame("108 00 00 00 90 01 10 27 00");
    ASSERT_TRUE(frame.has_value());

    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getMotorRpm().value(), 1000.0, 0.1);
    EXPECT_NEAR(result->getMotorTorqueNm().value(), 100.0, 0.5);
}

// ================================================
// Test Suite 6: Edge Cases and Error Handling
// ================================================

TEST(TeslaCANLiveDecoding, EdgeCase_UnknownCANIdReturnsDefaults) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla_model3", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla_model3", VehicleProtocol::CAN, TESLA_DBC));

    // Unknown CAN ID (not in Tesla DBC)
    CANFrame unknownFrame;
    unknownFrame.canId = 0x999; // 2457 decimal
    unknownFrame.data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    auto dbcFormat = canFrameToDBC(unknownFrame);
    auto result = service.processFrame(dbcFormat);

    EXPECT_TRUE(result.has_value()) << "Should return signal even for unknown CAN ID";
    EXPECT_FALSE(result->getMotorRpm().has_value());
    EXPECT_FALSE(result->getMotorTorqueNm().has_value());
    EXPECT_FALSE(result->getThrottlePercent().has_value());
    EXPECT_FALSE(result->getSteeringAngleDeg().has_value());
}

TEST(TeslaCANLiveDecoding, EdgeCase_ELM327PromptReturnsNullopt) {
    auto frame = ELM327Transport::parseCANFrame(">");
    EXPECT_FALSE(frame.has_value());
}

TEST(TeslaCANLiveDecoding, EdgeCase_ELM327ErrorReturnsNullopt) {
    auto frame = ELM327Transport::parseCANFrame("NO DATA");
    EXPECT_FALSE(frame.has_value());
}

TEST(TeslaCANLiveDecoding, EdgeCase_OBD2ResponseReturnsNullopt) {
    // Standard OBD2 response (not a CAN frame)
    auto frame = ELM327Transport::parseCANFrame("41 0C 1A F8");
    EXPECT_FALSE(frame.has_value());
}
