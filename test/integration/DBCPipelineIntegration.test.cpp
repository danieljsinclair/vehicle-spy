#include <gtest/gtest.h>
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"

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

    // Helper to create Tesla config
    VehicleConfig createTeslaConfig() {
        return VehicleConfig(
            "Model3CAN.dbc",
            "Tesla Model 3",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"SteeringAngle129", "steeringAngleDeg"}
            }
        );
    }

    // Helper to create Audi config
    VehicleConfig createAudiConfig() {
        return VehicleConfig(
            "vw_mlb.dbc",
            "Audi MLB Evo",
            std::unordered_map<std::string, std::string>{
                {"ESP_v_Signal", "speedKmh"}
            }
        );
    }
};

// ================================================
// Tesla Full Pipeline Integration Test
// ================================================

TEST_F(DBCPipelineIntegrationTest, TeslaFullPipeline) {
    // Step 1: Parse Model3CAN.dbc
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    
    // Step 2: Verify at least CAN IDs 264, 280, 297 exist
    ASSERT_NE(parseResult.getSignalsForCanId(264), nullptr) << "CAN 264 not found in DBC";
    ASSERT_NE(parseResult.getSignalsForCanId(280), nullptr) << "CAN 280 not found in DBC";
    ASSERT_NE(parseResult.getSignalsForCanId(297), nullptr) << "CAN 297 not found in DBC";

    // Step 3: Create VehicleConfig with Tesla signal mappings
    auto config = createTeslaConfig();

    // Step 4: Create DBCSignalTranslator
    DBCSignalTranslator translator(config, parseResult);

    // Step 5: Feed CAN 264 frame with known axleSpeed value
    // 1000 RPM = raw 10000 = 0x2710
    // CAN 264 = 0x0108: [0x08, 0x01, data_0..data_7]
    // DIR_axleSpeed at bit 40, 16-bit Intel: byte 5 = 0x10, byte 6 = 0x27
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264 (little-endian)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00  // axleSpeed = 1000 RPM
    };
    auto result264 = translator.translate(frame264);
    ASSERT_TRUE(result264.has_value()) << "Failed to translate CAN 264 frame";
    EXPECT_EQ(result264->getMotorRpm().value(), 1000.0);

    // Step 6: Feed CAN 280 frame with known accelPedalPos value
    // 75.2% throttle = raw 188 (75.2 / 0.4 = 188)
    // CAN 280 = 0x0118: [0x18, 0x01, data_0..data_7]
    // DI_accelPedalPos at bit 32, 8-bit Intel: byte 4 = 0xBC
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00  // throttle = 75.2%
    };
    auto result280 = translator.translate(frame280);
    ASSERT_TRUE(result280.has_value()) << "Failed to translate CAN 280 frame";
    EXPECT_NEAR(result280->getThrottlePercent().value(), 75.2, 0.1);

    // Step 7: Feed CAN 297 frame with known steering angle value
    // 0 degrees center = raw 8192 = 0x2000 (offset -819.2, scale 0.1)
    // CAN 297 = 0x0129: [0x29, 0x01, data_0..data_7]
    // SteeringAngle129 at bit 16, 14-bit Intel: byte 2 = 0x00, byte 3 = 0x20
    std::vector<std::uint8_t> frame297 = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00  // steering = 0.0 deg (raw=8192 at data[2..3])
    };
    auto result297 = translator.translate(frame297);
    ASSERT_TRUE(result297.has_value()) << "Failed to translate CAN 297 frame";
    EXPECT_NEAR(result297->getSteeringAngleDeg().value(), 0.0, 0.1);

    // Step 8: Verify all three fields populated in VehicleSignal
    // After processing all three frames, the final result should have all values
    EXPECT_EQ(result297->getMotorRpm().value(), 1000.0);
    EXPECT_NEAR(result297->getThrottlePercent().value(), 75.2, 0.1);
    EXPECT_NEAR(result297->getSteeringAngleDeg().value(), 0.0, 0.1);
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
    // 100 km/h = raw 10000 (100 / 0.01)
    // CAN 256 = 0x0100: [0x00, 0x01, data_0..data_7]
    // ESP_v_Signal at bit 0, 16-bit Intel: byte 0 = 0x10, byte 1 = 0x27
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // speed = 100 km/h
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
    // Step 1: Parse Model3CAN.dbc
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify specific signal names exist
    bool foundAxleSpeed = false;
    bool foundAccelPedal = false;
    bool foundSteeringAngle = false;

    for (const auto& [canId, signals] : parseResult.signalsByCanId) {
        for (const auto& sig : signals) {
            if (sig.name == "DIR_axleSpeed") foundAxleSpeed = true;
            if (sig.name == "DI_accelPedalPos") foundAccelPedal = true;
            if (sig.name == "SteeringAngle129") foundSteeringAngle = true;
        }
    }

    ASSERT_TRUE(foundAxleSpeed) << "DIR_axleSpeed signal not found in DBC";
    ASSERT_TRUE(foundAccelPedal) << "DI_accelPedalPos signal not found in DBC";
    ASSERT_TRUE(foundSteeringAngle) << "SteeringAngle129 signal not found in DBC";

    // Step 3: Verify CAN IDs 264 and 280 have signal definitions
    const auto* signals264 = parseResult.getSignalsForCanId(264);
    ASSERT_NE(signals264, nullptr) << "CAN 264 has no signal definitions";
    EXPECT_GT(signals264->size(), 0) << "CAN 264 should have at least one signal";

    const auto* signals280 = parseResult.getSignalsForCanId(280);
    ASSERT_NE(signals280, nullptr) << "CAN 280 has no signal definitions";
    EXPECT_GT(signals280->size(), 0) << "CAN 280 should have at least one signal";

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

    // Feed valid frame
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00  // axleSpeed = 1000 RPM
    };
    auto result1 = translator.translate(frame264);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->getMotorRpm().value(), 1000.0);

    // Reset translator
    translator.reset();

    // Feed another frame - previous state should be cleared
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00  // throttle = 75.2%
    };
    auto result2 = translator.translate(frame280);
    ASSERT_TRUE(result2.has_value());
    EXPECT_FALSE(result2->getMotorRpm().has_value()) << "Reset should clear accumulated state";
    EXPECT_NEAR(result2->getThrottlePercent().value(), 75.2, 0.1);
}

// ================================================
// Multi-Frame Accumulation
// ================================================

TEST_F(DBCPipelineIntegrationTest, MultipleFramesAccumulateState) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed CAN 264 — sets motorRpm
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00  // axleSpeed = 1000 RPM
    };
    auto r1 = translator.translate(frame264);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->getMotorRpm().value(), 1000.0);

    // Feed CAN 280 — sets throttlePercent, motorRpm preserved from accumulation
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00  // throttle = 75.2%
    };
    auto r2 = translator.translate(frame280);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getThrottlePercent().value(), 75.2, 0.1);
    EXPECT_EQ(r2->getMotorRpm().value(), 1000.0) << "motorRpm should persist from frame 264";

    // Feed CAN 297 — sets steeringAngleDeg, all three fields present
    std::vector<std::uint8_t> frame297 = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00  // steering = 0.0 deg
    };
    auto r3 = translator.translate(frame297);
    ASSERT_TRUE(r3.has_value());
    EXPECT_NEAR(r3->getSteeringAngleDeg().value(), 0.0, 0.1);
    EXPECT_EQ(r3->getMotorRpm().value(), 1000.0) << "motorRpm should persist from frame 264";
    EXPECT_NEAR(r3->getThrottlePercent().value(), 75.2, 0.1) << "throttle should persist from frame 280";
}

// ================================================
// Negative Steering Angle (signed + offset signal)
// ================================================

TEST_F(DBCPipelineIntegrationTest, NegativeSteeringAngle) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // SteeringAngle129: startBit=16, 14-bit, Intel, scale=0.1, offset=-819.2
    // Center (0 deg) = raw 8192 = 0x2000
    // Full left (-409.6 deg) = raw 4096 = 0x1000
    // 4096 in Intel at bit 16: data[2]=0x00, data[3]=0x10
    std::vector<std::uint8_t> frame297 = {
        0x29, 0x01,  // CAN ID 297
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00  // raw=4096 -> -409.6 deg
    };
    auto result = translator.translate(frame297);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSteeringAngleDeg().value(), -409.6, 0.1);
}

// ================================================
// Audi Max Speed (clamped by VehicleSignal)
// ================================================

TEST_F(DBCPipelineIntegrationTest, AudiSpeedAtMaxValue) {
    auto parseResult = parser.parseFile(getAudiDBCPath());
    auto config = createAudiConfig();
    DBCSignalTranslator translator(config, parseResult);

    // ESP_v_Signal: startBit=0, 16-bit, Intel, scale=0.01, offset=0
    // Raw 65535 (0xFFFF) = 655.35 km/h — clamped to 300.0 by VehicleSignal
    std::vector<std::uint8_t> frame256 = {
        0x00, 0x01,  // CAN ID 256
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // raw=65535 -> 655.35 km/h
    };
    auto result = translator.translate(frame256);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->getSpeedKmh().value(), 655.35, 0.01) << "Speed stored as-is (no clamping)";
}
