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

// Tesla Model 3 DBC content - minimal subset mirroring the canonical
// tesla_model3_party.dbc drive signals (resources/dbc/Model3CAN.dbc is that
// file verbatim). Only the four drive signals are included here:
//   DI_torqueActual   27|13@1- (2,0)    on CAN 264 (DI_torque)
//   DI_accelPedalPos  32|8@1+  (0.4,0)  on CAN 280 (DI_systemStatus)
//   DI_gear           21|3@1+  (1,0)    on CAN 280 (DI_systemStatus)
//   DI_vehicleSpeed   12|12@1+ (0.08,-40) on CAN 599 (DI_speed)
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

BU_: PARTY

BO_ 264 DI_torque: 8 PARTY
 SG_ DI_torqueActual : 27|13@1- (2,0) [-7500|7500] "Nm" PARTY

VAL_ 264 DI_torqueActual -4096 "SNA" ;

BO_ 280 DI_systemStatus: 8 PARTY
 SG_ DI_accelPedalPos : 32|8@1+ (0.4,0) [0|100] "%" PARTY
 SG_ DI_gear : 21|3@1+ (1,0) [0|7] "" PARTY

VAL_ 280 DI_accelPedalPos 255 "SNA" ;
VAL_ 280 DI_gear 1 "DI_GEAR_P" 0 "DI_GEAR_INVALID" 7 "DI_GEAR_SNA" 2 "DI_GEAR_R" 3 "DI_GEAR_N" 4 "DI_GEAR_D" ;

BO_ 599 DI_speed: 8 PARTY
 SG_ DI_vehicleSpeed : 12|12@1+ (0.08,-40) [-40|285] "kph" PARTY

VAL_ 599 DI_vehicleSpeed 4095 "SNA" ;
)DBC";

} // namespace

// ================================================
// Test Suite 1: ELM327 CAN Frame Parsing
// ================================================

TEST(TeslaCANLiveDecoding, ParseCANFrame264_NoPrefix) {
    // ELM327 monitor mode output for CAN ID 264 (0x108 hex)
    // Tesla CAN ID 264 decimal = 0x108 hex
    // RPM 1000: data[4]=0xE8, data[5]=0x03 (raw 0x03E8 = 1000)
    // Torque 100 Nm: data[2]=0x90, data[3]=0x01 (raw 400 * 0.25 = 100 Nm, 400 = 0x0190)
    auto frame = ELM327Transport::parseCANFrame("108 00 00 90 01 E8 03 BC 00");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->canId, 0x108); // 0x108 hex = 264 decimal
    EXPECT_EQ(frame->data.size(), 8);
    EXPECT_EQ(frame->data[0], 0x00);
    EXPECT_EQ(frame->data[1], 0x00);
    EXPECT_EQ(frame->data[2], 0x90);
    EXPECT_EQ(frame->data[3], 0x01);
    EXPECT_EQ(frame->data[4], 0xE8);
    EXPECT_EQ(frame->data[5], 0x03);
    EXPECT_EQ(frame->data[6], 0xBC);
    EXPECT_EQ(frame->data[7], 0x00);
}

TEST(TeslaCANLiveDecoding, ParseCANFrame264_WithTypePrefix) {
    // CAN frame with type byte (0x600 + type) for ELM327 monitor mode
    // Type 0x10 + 0x600 = 0x610, followed by CAN ID 0x108 and data
    auto frame = ELM327Transport::parseCANFrame("610 108 00 00 32 00 E8 03 BC 00");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->canId, 0x108);
    EXPECT_EQ(frame->data.size(), 8);
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

TEST(TeslaCANLiveDecoding, EndToEnd_CAN264_Torque) {
    DBCTranslationService service;
    auto& registry = service.registry();

    // Canonical signal mappings from tesla_model3_party.dbc
    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"DI_vehicleSpeed", "speedKmh"}
        },
        "",
        true // isCANProtocol
    );
    registry.registerVehicle("tesla", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla", VehicleProtocol::CAN, TESLA_DBC));

    // CAN 264 (0x108) DI_torqueActual = 100 Nm.
    //   27|13@1- scale 2 -> raw 50 -> data[3]=0x90, data[4]=0x01.
    auto frame = ELM327Transport::parseCANFrame("108 00 00 00 90 01 00 00 00");
    ASSERT_TRUE(frame.has_value());

    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 264 frame";
    EXPECT_NEAR(result->getMotorTorqueNm().value(), 100.0, 0.5);
}

TEST(TeslaCANLiveDecoding, EndToEnd_CAN280_Throttle) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"DI_vehicleSpeed", "speedKmh"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla", VehicleProtocol::CAN, TESLA_DBC));

    // CAN 280 (0x118) DI_accelPedalPos = 40%.
    //   32|8@1+ scale 0.4 -> raw 100 -> data[4]=0x64.
    auto frame = ELM327Transport::parseCANFrame("118 00 00 00 00 64 00 00 00");
    ASSERT_TRUE(frame.has_value());

    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 280 frame";
    EXPECT_NEAR(result->getThrottlePercent().value(), 40.0, 0.1);
}

// ================================================
// Test Suite 4: Multi-Frame Signal Accumulation
// ================================================

TEST(TeslaCANLiveDecoding, MultiFrame_AllSignalsAccumulate) {
    DBCTranslationService service;
    auto& registry = service.registry();

    VehicleConfig teslaConfig(
        "embedded_tesla.dbc",
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"DI_vehicleSpeed", "speedKmh"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla", VehicleProtocol::CAN, TESLA_DBC));

    // Feed CAN 264 - sets torque = 100 Nm
    auto frame264 = ELM327Transport::parseCANFrame("108 00 00 00 90 01 00 00 00");
    ASSERT_TRUE(frame264.has_value());
    auto result = service.processFrame(canFrameToDBC(*frame264));
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getMotorTorqueNm().value(), 100.0, 0.5);

    // Feed CAN 280 - sets throttle = 40%
    auto frame280 = ELM327Transport::parseCANFrame("118 00 00 00 00 64 00 00 00");
    ASSERT_TRUE(frame280.has_value());
    result = service.processFrame(canFrameToDBC(*frame280));
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getThrottlePercent().value(), 40.0, 0.1);
    EXPECT_NEAR(result->getMotorTorqueNm().value(), 100.0, 0.5) << "torque persists";

    // Feed CAN 599 - sets speed = 50 km/h (raw 1125 -> data[1]=0x50, data[2]=0x46)
    auto frame599 = ELM327Transport::parseCANFrame("257 00 50 46 00 00 00 00 00");
    ASSERT_TRUE(frame599.has_value());
    result = service.processFrame(canFrameToDBC(*frame599));
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSpeedKmh().value(), 50.0, 0.5);
    EXPECT_NEAR(result->getMotorTorqueNm().value(), 100.0, 0.5) << "torque persists";
    EXPECT_NEAR(result->getThrottlePercent().value(), 40.0, 0.1) << "throttle persists";
}

// ================================================
// Test Suite 5: DefaultVehicleConfigs Integration
// ================================================

TEST(TeslaCANLiveDecoding, DefaultConfig_TeslaModel3_Works) {
    DBCTranslationService service;

    // Register default Tesla config (canonical mappings)
    DefaultVehicleConfigs::registerAll(service.registry());

    ASSERT_TRUE(service.loadVehicleWithContent("tesla", VehicleProtocol::CAN, TESLA_DBC));

    // CAN 264 frame with DI_torqueActual = 100 Nm
    auto frame = ELM327Transport::parseCANFrame("108 00 00 00 90 01 00 00 00");
    ASSERT_TRUE(frame.has_value());

    auto dbcFormat = canFrameToDBC(*frame);
    auto result = service.processFrame(dbcFormat);

    ASSERT_TRUE(result.has_value());
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
        "embedded_tesla.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"DI_vehicleSpeed", "speedKmh"}
        },
        "",
        true
    );
    registry.registerVehicle("tesla", teslaConfig);

    ASSERT_TRUE(service.loadVehicleWithContent("tesla", VehicleProtocol::CAN, TESLA_DBC));

    // Unknown CAN ID (not in Tesla DBC)
    CANFrame unknownFrame;
    unknownFrame.canId = 0x999; // 2457 decimal
    unknownFrame.data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    auto dbcFormat = canFrameToDBC(unknownFrame);
    auto result = service.processFrame(dbcFormat);

    EXPECT_TRUE(result.has_value()) << "Should return signal even for unknown CAN ID";
    EXPECT_FALSE(result->getMotorTorqueNm().has_value());
    EXPECT_FALSE(result->getThrottlePercent().has_value());
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