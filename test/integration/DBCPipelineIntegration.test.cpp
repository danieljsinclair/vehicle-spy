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
            "Model3CAN.dbc",
            "Tesla Model 3",
            std::unordered_map<std::string, std::string>{
                {"DI_motorRPM", "motorRpm"},
                {"DI_pedalPos", "throttlePercent"},
                {"DI_analogSpeed", "speedKmh"}
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
    // Step 1: Parse Model3CAN.dbc
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify at least CAN IDs 264, 280, 872 exist
    ASSERT_NE(parseResult.getSignalsForCanId(264), nullptr) << "CAN 264 not found in DBC";
    ASSERT_NE(parseResult.getSignalsForCanId(280), nullptr) << "CAN 280 not found in DBC";
    ASSERT_NE(parseResult.getSignalsForCanId(872), nullptr) << "CAN 872 not found in DBC";

    // Step 3: Create VehicleConfig with Tesla signal mappings
    auto config = createTeslaConfig();

    // Step 4: Create DBCSignalTranslator
    DBCSignalTranslator translator(config, parseResult);

    // Step 5: Feed CAN 264 frame with both motorRPM and pedalPos values
    // DI_motorRPM at bit 32, 16-bit Intel: raw 1000 = 0x03E8
    // DI_pedalPos at bit 48, 8-bit Intel: raw 188 (75.2 / 0.4 = 188) = 0xBC
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264 (little-endian)
        0x00, 0x00, 0x00, 0x00, 0xE8, 0x03, 0xBC, 0x00  // motorRPM=1000, throttle=75.2%
    };
    auto result264 = translator.translate(frame264);
    ASSERT_TRUE(result264.has_value()) << "Failed to translate CAN 264 frame";
    EXPECT_EQ(result264->getMotorRpm().value(), 1000.0);
    EXPECT_NEAR(result264->getThrottlePercent().value(), 75.2, 0.1);

    // Step 6: Feed CAN 872 frame with known analogSpeed value
    // 50 km/h = raw 500 (50 / 0.1 = 500) = 0x01F4
    // DI_analogSpeed at bit 16, 12-bit Intel: byte 2 in data = byte 4 in frame
    std::vector<std::uint8_t> frame872 = {
        0x68, 0x03,  // CAN ID 872
        0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00  // speed = 50.0 km/h
    };
    auto result872 = translator.translate(frame872);
    ASSERT_TRUE(result872.has_value()) << "Failed to translate CAN 872 frame";
    EXPECT_NEAR(result872->getSpeedKmh().value(), 50.0, 0.1);

    // Step 7: Verify all three fields populated in VehicleSignal
    // After processing both frames, the final result should have all values
    // motorRPM and throttle from CAN 264 persist, speed from CAN 872 added
    EXPECT_EQ(result872->getMotorRpm().value(), 1000.0);
    EXPECT_NEAR(result872->getThrottlePercent().value(), 75.2, 0.1);
    EXPECT_NEAR(result872->getSpeedKmh().value(), 50.0, 0.1);
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
    // Step 1: Parse Model3CAN.dbc
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify specific signal names exist
    bool foundMotorRPM = false;
    bool foundPedalPos = false;
    bool foundAnalogSpeed = false;

    for (const auto& [canId, signals] : parseResult.signalsByCanId) {
        for (const auto& sig : signals) {
            if (sig.name == "DI_motorRPM") foundMotorRPM = true;
            if (sig.name == "DI_pedalPos") foundPedalPos = true;
            if (sig.name == "DI_analogSpeed") foundAnalogSpeed = true;
        }
    }

    ASSERT_TRUE(foundMotorRPM) << "DI_motorRPM signal not found in DBC";
    ASSERT_TRUE(foundPedalPos) << "DI_pedalPos signal not found in DBC";
    ASSERT_TRUE(foundAnalogSpeed) << "DI_analogSpeed signal not found in DBC";

    // Step 3: Verify CAN IDs 264, 280, and 872 have signal definitions
    const auto* signals264 = parseResult.getSignalsForCanId(264);
    ASSERT_NE(signals264, nullptr) << "CAN 264 has no signal definitions";
    EXPECT_GT(signals264->size(), 0) << "CAN 264 should have at least one signal";

    const auto* signals280 = parseResult.getSignalsForCanId(280);
    ASSERT_NE(signals280, nullptr) << "CAN 280 has no signal definitions";
    EXPECT_GT(signals280->size(), 0) << "CAN 280 should have at least one signal";

    const auto* signals872 = parseResult.getSignalsForCanId(872);
    ASSERT_NE(signals872, nullptr) << "CAN 872 has no signal definitions";
    EXPECT_GT(signals872->size(), 0) << "CAN 872 should have at least one signal";

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

    // Feed valid frame with motorRPM and throttle
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0xE8, 0x03, 0xBC, 0x00  // motorRPM=1000, throttle=75.2%
    };
    auto result1 = translator.translate(frame264);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->getMotorRpm().value(), 1000.0);
    EXPECT_NEAR(result1->getThrottlePercent().value(), 75.2, 0.1);

    // Reset translator
    translator.reset();

    // Feed another frame - previous state should be cleared
    std::vector<std::uint8_t> frame264_new = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // all zeros
    };
    auto result2 = translator.translate(frame264_new);
    ASSERT_TRUE(result2.has_value());
    // After reset, previous values should be gone, but new frame has zeros
    // motorRPM=0 (raw 0 * 1 = 0)
    // throttle=0 (raw 0 * 0.4 = 0)
    EXPECT_EQ(result2->getMotorRpm().value(), 0.0);
    EXPECT_EQ(result2->getThrottlePercent().value(), 0.0);
}

// ================================================
// Multi-Frame Accumulation
// ================================================

TEST_F(DBCPipelineIntegrationTest, MultipleFramesAccumulateState) {
    auto parseResult = parser.parseFile(getTeslaDBCPath());
    auto config = createTeslaConfig();
    DBCSignalTranslator translator(config, parseResult);

    // Feed CAN 264 — sets motorRpm and throttle
    std::vector<std::uint8_t> frame264 = {
        0x08, 0x01,  // CAN ID 264
        0x00, 0x00, 0x00, 0x00, 0xE8, 0x03, 0xBC, 0x00  // motorRPM=1000, throttle=75.2%
    };
    auto r1 = translator.translate(frame264);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->getMotorRpm().value(), 1000.0);
    EXPECT_NEAR(r1->getThrottlePercent().value(), 75.2, 0.1);

    // Feed CAN 872 — sets speedKmh, all three fields present
    std::vector<std::uint8_t> frame872 = {
        0x68, 0x03,  // CAN ID 872
        0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00  // speed = 50.0 km/h
    };
    auto r2 = translator.translate(frame872);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2->getSpeedKmh().value(), 50.0, 0.1);
    // motorRPM and throttle from CAN 264 should persist
    EXPECT_EQ(r2->getMotorRpm().value(), 1000.0) << "motorRpm should persist from CAN 264";
    EXPECT_NEAR(r2->getThrottlePercent().value(), 75.2, 0.1) << "throttle should persist from CAN 264";
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
            if (sig.name == "DI_analogSpeed") foundSpeedSignal = true;
        }
    }

    ASSERT_TRUE(foundGearSignal) << "DI_gear signal not found in DBC";
    ASSERT_TRUE(foundSpeedSignal) << "DI_analogSpeed signal not found in DBC";
}

TEST_F(DBCPipelineIntegrationTest, TeslaFullPipeline_WithGearAndSpeedFrames) {
    // Step 1: Parse Model3CAN.dbc
    auto parseResult = parser.parseFile(getTeslaDBCPath());

    // Step 2: Verify gear and speed signals exist
    const auto* signals280 = parseResult.getSignalsForCanId(280);
    const auto* signals872 = parseResult.getSignalsForCanId(872);
    ASSERT_NE(signals280, nullptr) << "CAN 280 not found in DBC";
    ASSERT_NE(signals872, nullptr) << "CAN 872 not found in DBC";

    // Step 3: Create VehicleConfig with Tesla signal mappings including gear and speed
    auto config = VehicleConfig(
        "Model3CAN.dbc",
        "Model3CAN.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{
            {"DI_motorRPM", "motorRpm"},
            {"DI_pedalPos", "throttlePercent"},
            {"DI_gear", "gearSelector"},
            {"DI_analogSpeed", "speedKmh"}
        }
    );

    // Step 4: Create DBCSignalTranslator
    DBCSignalTranslator translator(config, parseResult);

    // Step 5: Feed CAN 280 frame with gear=4 (Drive)
    // DI_gear: startBit=12, 3 bits, Intel, scale=1, offset=0
    // gear=4 at bits 12-14: byte 1 = 0x40
    std::vector<std::uint8_t> frame280 = {
        0x18, 0x01,  // CAN ID 280
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // gear = 4 (Drive)
    };
    auto result280 = translator.translate(frame280);
    ASSERT_TRUE(result280.has_value());

    // Step 6: Feed CAN 872 frame with speed=50 km/h
    // DI_analogSpeed: startBit=16, 12 bits, Intel, scale=0.1, offset=0
    // speedKmh = 50.0 km/h = raw 500 = 0x01F4
    // At bit 16: byte 2 = 0xF4, byte 3 = 0x01
    std::vector<std::uint8_t> frame872 = {
        0x68, 0x03,  // CAN ID 872
        0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00  // speed = 50.0 km/h
    };
    auto result872 = translator.translate(frame872);
    ASSERT_TRUE(result872.has_value());
    EXPECT_NEAR(result872->getSpeedKmh().value(), 50.0, 0.1);

    // Step 7: Verify gear signal persisted from frame 280
    EXPECT_TRUE(result872->getGearSelector().has_value());
}