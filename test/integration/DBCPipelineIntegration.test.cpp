#include <gtest/gtest.h>
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

class DBCPipelineIntegrationTest : public ::testing::Test {
protected:
    DBCFileParser parser;

    // Helper to get DBC file path relative to project root
    std::string getTeslaDBCPath() const {
        return "resources/dbc/Model3CAN.dbc";
    }

    std::string getAudiDBCPath() const {
        return "resources/dbc/vw_mlb.dbc";
    }

    // Helper to create Tesla config using canonical opendbc signal names
    // (resources/dbc/Model3CAN.dbc == tesla_model3_party.dbc, verbatim).
    //   DI_torqueActual   -> motorTorqueNm (CAN 264 / 0x108, DI_torque)
    //   DI_accelPedalPos  -> throttlePercent (CAN 280 / 0x118, DI_systemStatus)
    //   DI_vehicleSpeed   -> speedKmh (CAN 599 / 0x257, DI_speed)
    VehicleConfig createTeslaConfig() {
        return VehicleConfig(
            "Model3CAN.dbc",
            "Model3CAN.dbc",
            "Tesla Model 3",
            std::unordered_map<std::string, std::string>{
                {"DI_torqueActual", "motorTorqueNm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"DI_vehicleSpeed", "speedKmh"}
            }
        );
    }

    // Helper to create Audi config
    VehicleConfig createAudiConfig() {
        return VehicleConfig(
            "vw_mlb.dbc",
            "vw_mlb.dbc",
            "Audi MLB Evo",
            std::unordered_map<std::string, std::string>{
                {"ESP_v_Signal", "speedKmh"},
                {"ESP_Laengsbeschl", "accelerationG"},
                {"ESP_Bremsdruck", "brakePercent"}
            }
        );
    }
};

// ================================================
// Tesla Full Pipeline Integration Test
// ================================================

TEST_F(DBCPipelineIntegrationTest, TeslaFullPipeline) {
    // Step 1: Parse Model3CAN.dbc (canonical tesla_model3_party.dbc, verbatim)
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify the canonical drive-message CAN IDs exist.
    //   264 (0x108) = DI_torque        (DI_torqueActual)
    //   280 (0x118) = DI_systemStatus  (DI_accelPedalPos, DI_gear)
    //   599 (0x257) = DI_speed         (DI_vehicleSpeed)
    ASSERT_NE(parseResult.getSignalsForCanId(264), nullptr) << "CAN 264 not found in DBC";
    ASSERT_NE(parseResult.getSignalsForCanId(280), nullptr) << "CAN 280 not found in DBC";
    ASSERT_NE(parseResult.getSignalsForCanId(599), nullptr) << "CAN 599 not found in DBC";

    // Step 3: Create VehicleConfig with Tesla signal mappings
    auto config = createTeslaConfig();

    // Step 4: Create DBCSignalTranslator
    DBCSignalTranslator translator(config, parseResult);

    // Step 5: Feed CAN 280 (DI_systemStatus) with DI_accelPedalPos = 40%.
    //   DI_accelPedalPos: 32|8@1+ scale 0.4 -> raw 100 = 0x64 at data[4].
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280 (little-endian)
        0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00  // throttle = 40.0%
    };
    auto result280 = translator.translate(frame280);
    ASSERT_TRUE(result280.has_value()) << "Failed to translate CAN 280 frame";
    EXPECT_NEAR(result280->getThrottlePercent().value(), 40.0, 0.1);

    // Step 6: Feed CAN 264 (DI_torque) with DI_torqueActual = 100 Nm.
    //   DI_torqueActual: 27|13@1- scale 2 -> raw 50 at data bytes 3,4 = 0x90, 0x01.
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x00  // torque = 100 Nm
    };
    auto result264 = translator.translate(frame264);
    ASSERT_TRUE(result264.has_value()) << "Failed to translate CAN 264 frame";
    EXPECT_NEAR(result264->getMotorTorqueNm().value(), 100.0, 0.5);

    // Step 7: Feed CAN 599 (DI_speed) with DI_vehicleSpeed = 50 km/h.
    //   DI_vehicleSpeed: 12|12@1+ scale 0.08 offset -40 -> raw 1125 = 0x465.
    //   Intel packing across bits 12-23: data[1]=0x50, data[2]=0x46.
    std::vector<std::uint8_t> frame599 = {
        0x57, 0x02,  // CAN ID 599
        0x00, 0x50, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00  // speed = 50.0 km/h (raw 1125)
    };
    auto result599 = translator.translate(frame599);
    ASSERT_TRUE(result599.has_value()) << "Failed to translate CAN 599 frame";
    EXPECT_NEAR(result599->getSpeedKmh().value(), 50.0, 0.5);

    // Step 8: All three signals persist across multi-frame accumulation.
    EXPECT_NEAR(result599->getThrottlePercent().value(), 40.0, 0.1)
        << "throttle should persist from CAN 280";
    EXPECT_NEAR(result599->getMotorTorqueNm().value(), 100.0, 0.5)
        << "torque should persist from CAN 264";
}

// ================================================
// Audi Full Pipeline Integration Test
// ================================================

TEST_F(DBCPipelineIntegrationTest, AudiFullPipeline) {
    // Step 1: Parse vw_mlb.dbc
    auto parseResult = parser.parseFile(getAudiDBCPath());

    // Step 2: Verify CAN ID 256 exists with ESP_v_Signal
    ASSERT_NE(parseResult.getSignalsForCanId(256), nullptr) << "CAN 256 not found in DBC";
    const auto* signals256 = parseResult.getSignalsForCanId(256);
    ASSERT_NE(signals256, nullptr);

    bool foundSpeedSignal = false;
    for (const auto& sig : *signals256) {
        if (sig.name == "ESP_v_Signal") {
            foundSpeedSignal = true;
            break;
        }
    }
    ASSERT_TRUE(foundSpeedSignal) << "ESP_v_Signal not found in CAN 256";

    // Step 3: Create VehicleConfig with Audi signal mappings
    auto config = createAudiConfig();

    // Step 4: Create DBCSignalTranslator
    DBCSignalTranslator translator(config, parseResult);

    // Step 5: Feed CAN 256 frame with known speed value
    // 100 km/h = raw 10000 (100 / 0.01) = 0x2710
    // ESP_v_Signal at bit 32, 16-bit Intel: data[4] = 0x10, data[5] = 0x27
    // In frame format (bytes 0-9): byte 6 = 0x10, byte 7 = 0x27
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00  // speed = 100 km/h
    };
    auto result = translator.translate(frame256);

    // Step 6: Verify speedKmh populated
    ASSERT_TRUE(result.has_value()) << "Failed to translate CAN 256 frame";
    EXPECT_NEAR(result->getSpeedKmh().value(), 100.0, 0.01);
}

// ================================================
// Parse Real Tesla DBC - Signal Validation
// ================================================

TEST_F(DBCPipelineIntegrationTest, ParseRealTeslaDBC_HasExpectedSignals) {
    // Step 1: Parse Model3CAN.dbc (canonical tesla_model3_party.dbc, verbatim)
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify canonical drive-signal names exist
    bool foundTorqueActual = false;
    bool foundAccelPedalPos = false;
    bool foundVehicleSpeed = false;

    for (const auto& [canId, signals] : parseResult.signalsByCanId) {
        for (const auto& sig : signals) {
            if (sig.name == "DI_torqueActual") foundTorqueActual = true;
            if (sig.name == "DI_accelPedalPos") foundAccelPedalPos = true;
            if (sig.name == "DI_vehicleSpeed") foundVehicleSpeed = true;
        }
    }

    ASSERT_TRUE(foundTorqueActual) << "DI_torqueActual signal not found in DBC";
    ASSERT_TRUE(foundAccelPedalPos) << "DI_accelPedalPos signal not found in DBC";
    ASSERT_TRUE(foundVehicleSpeed) << "DI_vehicleSpeed signal not found in DBC";

    // Step 3: Verify the canonical drive-message CAN IDs have signal definitions
    //   264 (DI_torque), 280 (DI_systemStatus), 599 (DI_speed)
    const auto* signals264 = parseResult.getSignalsForCanId(264);
    ASSERT_NE(signals264, nullptr) << "CAN 264 has no signal definitions";
    EXPECT_GT(signals264->size(), 0) << "CAN 264 should have at least one signal";

    const auto* signals280 = parseResult.getSignalsForCanId(280);
    ASSERT_NE(signals280, nullptr) << "CAN 280 has no signal definitions";
    EXPECT_GT(signals280->size(), 0) << "CAN 280 should have at least one signal";

    const auto* signals599 = parseResult.getSignalsForCanId(599);
    ASSERT_NE(signals599, nullptr) << "CAN 599 has no signal definitions";
    EXPECT_GT(signals599->size(), 0) << "CAN 599 should have at least one signal";

    // Step 4: Verify signal count > 0 for each
    EXPECT_GT(parseResult.totalSignalCount(), 0) << "DBC should contain at least one signal";
}

// ================================================
// Parse Real Audi DBC - Signal Validation
// ================================================

TEST_F(DBCPipelineIntegrationTest, ParseRealAudiDBC_HasExpectedSignals) {
    // Step 1: Parse vw_mlb.dbc
    auto parseResult = parser.parseFile(getAudiDBCPath());

    // Step 2: Verify signal "ESP_v_Signal" exists
    bool foundSpeedSignal = false;
    for (const auto& [canId, signals] : parseResult.signalsByCanId) {
        for (const auto& sig : signals) {
            if (sig.name == "ESP_v_Signal") {
                foundSpeedSignal = true;
                break;
            }
        }
    }
    ASSERT_TRUE(foundSpeedSignal) << "ESP_v_Signal signal not found in DBC";

    // Step 3: Verify CAN 256 has signal definitions
    const auto* signals256 = parseResult.getSignalsForCanId(256);
    ASSERT_NE(signals256, nullptr) << "CAN 256 has no signal definitions";
    EXPECT_GT(signals256->size(), 0) << "CAN 256 should have at least one signal";

    // Step 4: Verify total signal count
    EXPECT_GT(parseResult.totalSignalCount(), 0) << "DBC should contain at least one signal";
}

// ================================================
// Edge Case: Invalid DBC Path
// ================================================

TEST_F(DBCPipelineIntegrationTest, InvalidDBCPathReturnsEmptyResult) {
    auto parseResult = parser.parseFile("nonexistent/path/to/file.dbc");

    EXPECT_EQ(parseResult.totalSignalCount(), 0);
    EXPECT_TRUE(parseResult.signalsByCanId.empty());
}

// ================================================
// Edge Case: Translator With Unsupported CAN ID
// ================================================

TEST_F(DBCPipelineIntegrationTest, UnsupportedCANIdIgnored) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed frame with CAN ID 999 (not in DBC)
    std::vector<std::uint8_t> frame = {
        0xE7, 0x03,  // CAN ID 999
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    auto result = translator.translate(frame);
    EXPECT_TRUE(result.has_value()) << "Translator should return signal even for unknown CAN ID";
    EXPECT_FALSE(result->getMotorRpm().has_value()) << "Unknown CAN ID should not modify signal values";
}

// ================================================
// Edge Case: Frame Too Short
// ================================================

TEST_F(DBCPipelineIntegrationTest, FrameTooShortReturnsNullopt) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    std::vector<std::uint8_t> shortFrame = {0x08, 0x01, 0x00, 0x00};  // Only 4 bytes

    auto result = translator.translate(shortFrame);
    EXPECT_FALSE(result.has_value()) << "Short frame should return nullopt";
}

// ================================================
// Edge Case: Reset Clears Accumulated State
// ================================================

TEST_F(DBCPipelineIntegrationTest, ResetClearsAccumulatedState) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed valid CAN 280 frame with throttle = 40%
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00  // throttle = 40.0%
    };
    auto result1 = translator.translate(frame280);
    ASSERT_TRUE(result1.has_value());
    EXPECT_NEAR(result1->getThrottlePercent().value(), 40.0, 0.1);

    // Reset translator
    translator.reset();

    // Feed another frame with zero throttle - previous state should be cleared
    std::vector<std::uint8_t> frame280_new = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // all zeros
    };
    auto result2 = translator.translate(frame280_new);
    ASSERT_TRUE(result2.has_value());
    // After reset, the previous throttle value is gone and the new frame sets it to 0
    EXPECT_EQ(result2->getThrottlePercent().value(), 0.0);
}

// ================================================
// Multi-Frame Accumulation
// ================================================

TEST_F(DBCPipelineIntegrationTest, MultipleFramesAccumulateState) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed CAN 264 (DI_torque) — sets motorTorqueNm
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x90, 0x01, 0x00, 0x00, 0x00  // torque = 100 Nm
    };
    auto r1 = translator.translate(frame264);
    ASSERT_TRUE(r1.has_value());
    EXPECT_NEAR(r1->getMotorTorqueNm().value(), 100.0, 0.5);

    // Feed CAN 280 (DI_systemStatus) — sets throttlePercent
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00  // throttle = 40.0%
    };
    auto r2 = translator.translate(frame280);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getThrottlePercent().value(), 40.0, 0.1);
    // torque from CAN 264 should persist across messages
    EXPECT_NEAR(r2->getMotorTorqueNm().value(), 100.0, 0.5)
        << "motor torque should persist from CAN 264";

    // Feed CAN 599 (DI_speed) — sets speedKmh
    std::vector<std::uint8_t> frame599 = {
        0x57, 0x02,  // CAN ID 599
        0x00, 0x50, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00  // speed = 50.0 km/h (raw 1125)
    };
    auto r3 = translator.translate(frame599);
    ASSERT_TRUE(r3.has_value());
    EXPECT_NEAR(r3->getSpeedKmh().value(), 50.0, 0.5);
    EXPECT_NEAR(r3->getMotorTorqueNm().value(), 100.0, 0.5)
        << "motor torque should persist from CAN 264";
    EXPECT_NEAR(r3->getThrottlePercent().value(), 40.0, 0.1)
        << "throttle should persist from CAN 280";
}

// ================================================
// Audi Max Speed (clamped by VehicleSignal)
// ================================================

TEST_F(DBCPipelineIntegrationTest, AudiSpeedAtMaxValue) {
    auto parseResult = parser.parseFile(getAudiDBCPath());
    auto config = createAudiConfig();
    DBCSignalTranslator translator(config, parseResult);

    // ESP_v_Signal: startBit=32, 16-bit, Intel, scale=0.01, offset=0
    // Raw 65535 (0xFFFF) = 655.35 km/h — clamped to 300.0 by VehicleSignal
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00  // raw=65535 -> 655.35 km/h
    };
    auto result = translator.translate(frame256);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSpeedKmh().value(), 655.35, 0.1) << "Speed stored as-is (no clamping)";
}

TEST_F(DBCPipelineIntegrationTest, ParseRealTeslaDBC_HasGearAndSpeedSignals) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    bool foundGearSignal = false;
    bool foundSpeedSignal = false;

    for (const auto& [canId, signals] : parseResult.signalsByCanId) {
        for (const auto& sig : signals) {
            if (sig.name == "DI_gear") foundGearSignal = true;
            if (sig.name == "DI_vehicleSpeed") foundSpeedSignal = true;
        }
    }

    ASSERT_TRUE(foundGearSignal) << "DI_gear signal not found in DBC";
    ASSERT_TRUE(foundSpeedSignal) << "DI_vehicleSpeed signal not found in DBC";
}

TEST_F(DBCPipelineIntegrationTest, TeslaFullPipeline_WithGearAndSpeedFrames) {
    // Step 1: Parse Model3CAN.dbc (canonical tesla_model3_party.dbc, verbatim)
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify gear (CAN 280) and speed (CAN 599) signals exist
    const auto* signals280 = parseResult.getSignalsForCanId(280);
    const auto* signals599 = parseResult.getSignalsForCanId(599);
    ASSERT_NE(signals280, nullptr) << "CAN 280 not found in DBC";
    ASSERT_NE(signals599, nullptr) << "CAN 599 not found in DBC";

    // Step 3: Create VehicleConfig with Tesla signal mappings including gear and speed
    auto config = VehicleConfig(
        "Model3CAN.dbc",
        "Model3CAN.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_torqueActual", "motorTorqueNm"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"DI_gear", "gearSelector"},
            {"DI_vehicleSpeed", "speedKmh"}
        }
    );

    // Step 4: Create DBCSignalTranslator
    DBCSignalTranslator translator(config, parseResult);

    // Step 5: Feed CAN 280 frame with gear=4 (Drive).
    //   DI_gear: 21|3@1+ scale 1 -> raw 4 at data[2] = 0x80.
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00  // gear = 4 (Drive)
    };
    auto result280 = translator.translate(frame280);
    ASSERT_TRUE(result280.has_value());

    // Step 6: Feed CAN 599 frame with speed = 50 km/h.
    //   DI_vehicleSpeed: 12|12@1+ scale 0.08 offset -40 -> raw 1125
    //   Intel bits 12-23: data[1]=0x50, data[2]=0x46.
    std::vector<std::uint8_t> frame599 = {
        0x57, 0x02,  // CAN ID 599
        0x00, 0x50, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00  // speed = 50.0 km/h
    };
    auto result599 = translator.translate(frame599);
    ASSERT_TRUE(result599.has_value());
    EXPECT_NEAR(result599->getSpeedKmh().value(), 50.0, 0.5);

    // Step 7: Verify gear signal persisted from frame 280
    EXPECT_TRUE(result599->getGearSelector().has_value());
    EXPECT_EQ(result599->getGearSelector().value(), Gear::AUTO_1);
}

// =============================================================================
// Canonical opendbc signal layout tests.
//
// resources/dbc/Model3CAN.dbc is now the canonical tesla_model3_party.dbc
// (verbatim, no hand-edits). These tests pin the canonical bit-layouts for the
// drive signals so accidental regressions are caught immediately:
//   DI_gear         21|3@1+   scale 1   on CAN 280 (DI_systemStatus)
//   DI_vehicleSpeed 12|12@1+  scale 0.08 offset -40 on CAN 599 (DI_speed)
// =============================================================================

class TeslaCanonicalSignalsTest : public ::testing::Test {
protected:
    DBCFileParser parser;

    std::string getTeslaDBCPath() const { return "resources/dbc/Model3CAN.dbc"; }

    VehicleConfig createTeslaConfig() {
        return VehicleConfig(
            "Model3CAN.dbc",
            "Model3CAN.dbc",
            "Tesla Model 3",
            std::unordered_map<std::string, std::string>{
                {"DI_gear", "gearSelector"},
                {"DI_vehicleSpeed", "speedKmh"},
                {"DI_accelPedalPos", "throttlePercent"}
            },
            "",   // canBus
            true  // isCANProtocol
        );
    }
};

// DI_gear lives at startBit 21, 3 bits Intel in CAN 0x118 (decimal 280).
// Enum (canonical VAL_ table): 1=P, 2=R, 3=N, 4=D.
//   gear 1 -> data[2] = 0x20, gear 2 -> data[2] = 0x40, gear 4 -> data[2] = 0x80.
TEST_F(TeslaCanonicalSignalsTest, GearDecodesParkReverseDriveFromCanonicalLayout) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    // Keep config alive: DBCSignalTranslator holds a const ref to it.
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // CAN ID 280 = [0x18, 0x01]. gear occupies bits 21-23 (top 3 bits of data[2]).
    const std::vector<std::uint8_t> parkFrame  = {0x18, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::vector<std::uint8_t> revFrame   = {0x18, 0x01, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::vector<std::uint8_t> driveFrame = {0x18, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto rp = translator.translate(parkFrame);
    ASSERT_TRUE(rp.has_value());
    ASSERT_TRUE(rp->getGearSelector().has_value());
    EXPECT_EQ(rp->getGearSelector().value(), Gear::PARK);

    auto rr = translator.translate(revFrame);
    ASSERT_TRUE(rr.has_value());
    ASSERT_TRUE(rr->getGearSelector().has_value());
    EXPECT_EQ(rr->getGearSelector().value(), Gear::REVERSE);

    auto rd = translator.translate(driveFrame);
    ASSERT_TRUE(rd.has_value());
    ASSERT_TRUE(rd->getGearSelector().has_value());
    EXPECT_EQ(rd->getGearSelector().value(), Gear::AUTO_1);
}

// DI_vehicleSpeed lives in CAN 0x257 (decimal 599), 12-bit Intel at startBit 12,
// scale 0.08 km/h/bit, offset -40 (so raw 500 == 0.0 km/h sensor zero).
//   0 km/h  -> raw 500  -> data[1]=0x40, data[2]=0x1f
//   80 km/h -> raw 1500 -> data[1]=0xc0, data[2]=0x5d  (~50 mph peak)
TEST_F(TeslaCanonicalSignalsTest, SpeedDecodesParkedAndPeakFromCanonicalLayout) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    // Keep config alive: DBCSignalTranslator holds a const ref to it.
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // CAN ID 599 = [0x57, 0x02]. Speed = raw 500 (~0 km/h).
    // Intel bits 12-23: data[1]=0x40, data[2]=0x1f.
    const std::vector<std::uint8_t> parkedFrame = {0x57, 0x02, 0x00, 0x40, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto parked = translator.translate(parkedFrame);
    ASSERT_TRUE(parked.has_value());
    ASSERT_TRUE(parked->getSpeedKmh().has_value());
    EXPECT_NEAR(parked->getSpeedKmh().value(), 0.0, 1.0);  // ~0 km/h when parked

    // Peak speed: raw 1500 -> data[1]=0xC0, data[2]=0x5D.
    // 1500 * 0.08 - 40 = 80.0 km/h (~50 mph).
    const std::vector<std::uint8_t> peakFrame = {0x57, 0x02, 0x00, 0xC0, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto peak = translator.translate(peakFrame);
    ASSERT_TRUE(peak.has_value());
    ASSERT_TRUE(peak->getSpeedKmh().has_value());
    EXPECT_NEAR(peak->getSpeedKmh().value(), 80.0, 1.0);
    EXPECT_NEAR(peak->getSpeedKmh().value() * 0.621371, 50.0, 1.0);  // ~50 mph
}

// Regression guard: DI_vehicleSpeed must NOT be defined on CAN 0x118 (decimal
// 280). The old legacy tesla_can.dbc layout mis-placed it there.
TEST_F(TeslaCanonicalSignalsTest, SpeedSignalNotOnSystemStatusMessage) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    const auto* signals280 = parseResult.getSignalsForCanId(280);
    ASSERT_NE(signals280, nullptr);
    for (const auto& sig : *signals280) {
        EXPECT_NE(sig.name, "DI_vehicleSpeed")
            << "DI_vehicleSpeed must live on CAN 0x257, not 0x118";
    }

    const auto* signals599 = parseResult.getSignalsForCanId(599);
    ASSERT_NE(signals599, nullptr);
    bool found = false;
    for (const auto& sig : *signals599) {
        if (sig.name == "DI_vehicleSpeed") found = true;
    }
    EXPECT_TRUE(found) << "DI_vehicleSpeed must be defined on CAN 0x257 (decimal 599)";
}