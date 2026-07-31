#include <gtest/gtest.h>
#include <memory>
#include "vehicle-sim/domain/VehicleSignalFactory.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

class VehicleSignalFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = std::make_unique<VehicleConfig>(
            "tesla_model3.dbc",
            "tesla_model3.dbc",
            "Tesla Model Y",
            std::unordered_map<std::string, std::string>{
                {"DIR_axleSpeed", "motorRpm"},
                {"DIR_torqueActual", "motorTorqueNm"},
                {"DI_accelPedalPos", "throttlePercent"},
                {"DI_brakePedal", "brakePercent"},
                {"SteeringAngle129", "steeringAngleDeg"}
            },
            "",  // canBus
            false  // isCANProtocol
        );

        parseResult_.signalsByCanId[264].emplace_back(DBCSignalParams{
            264, "DIR_axleSpeed", 40, 16, DBCByteOrder::Intel, 0.1, 0.0, true, "RPM", -2750.0, 2750.0
        });
        parseResult_.signalsByCanId[264].emplace_back(DBCSignalParams{
            264, "DIR_torqueActual", 27, 13, DBCByteOrder::Intel, 2.0, 0.0, true, "Nm", -7500.0, 7500.0
        });
        parseResult_.signalsByCanId[280].emplace_back(DBCSignalParams{
            280, "DI_accelPedalPos", 32, 8, DBCByteOrder::Motorola, 0.4, 0.0, false, "%", 0.0, 100.0
        });
        parseResult_.signalsByCanId[297].emplace_back(DBCSignalParams{
            297, "SteeringAngle129", 16, 14, DBCByteOrder::Motorola, 0.1, -819.2, false, "deg", -819.2, 819.1
        });
    }

    // Helper to create Tesla DI_gear signal with value table
    void addTeslaGearSignal(DBCParseResult& result) {
        result.signalsByCanId[280].emplace_back(DBCSignalParams{
            280, "DI_gear", 12, 3, DBCByteOrder::Intel, 1.0, 0.0, true, "", 0.0, 7.0,
            {{0, "DI_GEAR_INVALID"},
             {1, "DI_GEAR_P"},
             {2, "DI_GEAR_R"},
             {3, "DI_GEAR_N"},
             {4, "DI_GEAR_D"},
             {7, "DI_GEAR_SNA"}}
        });
    }

    std::unique_ptr<VehicleConfig> config_;
    DBCParseResult parseResult_;
};

TEST_F(VehicleSignalFactoryTest, BuildFromSingleCanFrameWithOneMappedSignal) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getTimestampUtcMs(), 1234567890);
    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
}

TEST_F(VehicleSignalFactoryTest, BuildFromMultipleCanFramesWithMultipleSignals) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};
    // DI_accelPedalPos is Motorola 32|8@0+, scale 0.4. startBit 32 = byte 4
    // physical bit 0 (MSB); raw 3 (-> 1.2%) encodes as byte4=0x00, byte5=0x06
    // (cantools-verified).
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00};
    frames[297] = {0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(signal.getThrottlePercent().value(), 1.2, 0.01);
    // SteeringAngle129 is Motorola 16|14@0+, scale 0.1 off -819.2. The byte
    // pattern frame[2]=0x20 leaves raw 0 -> physical -819.2 (cantools-verified).
    EXPECT_NEAR(signal.getSteeringAngleDeg().value(), -819.2, 0.01);
}

TEST_F(VehicleSignalFactoryTest, UnmappedSignalsDefaultToNullopt) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    // speedKmh, accelerationG, motorHvVoltage, motorHvCurrent have no signal mappings
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    EXPECT_FALSE(signal.getMotorHvVoltage().has_value());
    EXPECT_FALSE(signal.getMotorHvCurrent().has_value());
    // motorTorqueNm IS mapped (DIR_torqueActual), CAN 264 provides raw 0 -> value 0.0
    EXPECT_TRUE(signal.getMotorTorqueNm().has_value());
}

TEST_F(VehicleSignalFactoryTest, MissingCanFramesProduceDefaultNullopt) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSteeringAngleDeg().has_value());
    EXPECT_FALSE(signal.getBrakePercent().has_value());
}

TEST_F(VehicleSignalFactoryTest, FullIntegrationRealTeslaDBCPatterns) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};
    // DI_accelPedalPos Motorola 32|8@0+ scale 0.4. raw 100 (-> 40.0%) encodes
    // as byte4=0x00, byte5=0xC8 (cantools-verified).
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x00, 0x00};
    frames[297] = {0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
    EXPECT_NEAR(signal.getThrottlePercent().value(), 40.0, 0.01);
    // SteeringAngle129 Motorola 16|14@0+; frame[2]=0x20 -> raw 0 -> -819.2.
    EXPECT_NEAR(signal.getSteeringAngleDeg().value(), -819.2, 0.01);
}

TEST_F(VehicleSignalFactoryTest, BuildFromEmptyFramesReturnsDefaultSignal) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;

    auto signal = factory.build(frames, 1234567890);

    EXPECT_EQ(signal.getTimestampUtcMs(), 1234567890);
    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
    EXPECT_FALSE(signal.getAccelerationG().has_value());
    EXPECT_FALSE(signal.getBrakePercent().has_value());
    EXPECT_FALSE(signal.getSteeringAngleDeg().has_value());
    EXPECT_FALSE(signal.getMotorRpm().has_value());
    EXPECT_FALSE(signal.getMotorHvVoltage().has_value());
    EXPECT_FALSE(signal.getMotorHvCurrent().has_value());
    EXPECT_FALSE(signal.getMotorTorqueNm().has_value());
}

TEST_F(VehicleSignalFactoryTest, SignalWithNegativeTorqueValue) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x04, 0xE0, 0xFF, 0xFF, 0xFF};

    auto signal = factory.build(frames, 1234567890);

    // motorRpm IS mapped (DIR_axleSpeed), so it has a value from this frame
    EXPECT_TRUE(signal.getMotorRpm().has_value());
    EXPECT_EQ(signal.getMotorTorqueNm().value(), -2048.0);
}

TEST_F(VehicleSignalFactoryTest, GearSelectorDefaultsToNulloptWhenNotMapped) {
    VehicleSignalFactory factory(*config_, parseResult_);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST_F(VehicleSignalFactoryTest, GearCodeFourMapsToAuto1) {
    // CAN 280 frame with DI_gear = 4 (Drive)
    // DI_gear: startBit=12, 3 bits, Intel, scale=1, offset=0
    // gear=4 = binary 100, at bits 12-14 (byte 1, bit 4)
    // byte 1 = 0x40 (binary 01000000)
    // Expected: gearSelector = Gear::AUTO_1 (0x1001)
    auto configWithGear = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_gear", "gearSelector"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult gearParseResult;
    addTeslaGearSignal(gearParseResult);

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[280] = {0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    ASSERT_TRUE(signal.getGearSelector().has_value());
    EXPECT_EQ(signal.getGearSelector().value(), Gear::AUTO_1);
}

TEST_F(VehicleSignalFactoryTest, AnalogSpeed500MapsToFiftyKmh) {
    // CAN 872 frame with DI_analogSpeed = 500
    // DI_analogSpeed: startBit=16, 12 bits, Intel, scale=0.1, offset=0
    // speedKmh = 500 * 0.1 = 50.0 km/h
    // raw value 500 = 0x01F4
    // At bit 16: byte 2 = 0xF4, byte 3 = 0x01
    auto configWithSpeed = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_analogSpeed", "speedKmh"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult speedParseResult;
    speedParseResult.signalsByCanId[872].emplace_back(DBCSignalParams{
        872, "DI_analogSpeed", 16, 12, DBCByteOrder::Intel, 0.1, 0.0, true, "speed", 0.0, 150.0
    });

    VehicleSignalFactory factory(*configWithSpeed, speedParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[872] = {0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    EXPECT_NEAR(signal.getSpeedKmh().value(), 50.0, 0.1);
}

TEST_F(VehicleSignalFactoryTest, GearCodeZeroReturnsNullopt) {
    // DI_gear = 0 (INVALID)
    // DBC VAL_ table defines 0 as "DI_GEAR_INVALID"
    // Expected: nullopt (invalid signals filtered out)
    auto configWithGear = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_gear", "gearSelector"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult gearParseResult;
    addTeslaGearSignal(gearParseResult);

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[280] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST_F(VehicleSignalFactoryTest, GearCodeSevenReturnsNullopt) {
    // DI_gear = 7 (SNA - Signal Not Available)
    // DBC VAL_ table defines 7 as "DI_GEAR_SNA"
    // Expected: nullopt (SNA signals filtered out)
    auto configWithGear = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_gear", "gearSelector"}
        },
        "",  // canBus
        false  // isCANProtocol
    );

    DBCParseResult gearParseResult;
    addTeslaGearSignal(gearParseResult);

    VehicleSignalFactory factory(*configWithGear, gearParseResult);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    // gear=7 = binary 111 = 0xE0 at bit position 12 (byte 1, bits 4-6)
    frames[280] = {0x00, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1234567890);

    EXPECT_FALSE(signal.getGearSelector().has_value());
}

// --- Blind characterisation contracts for S134 (resolveMappings/build) ---
// These lock the externally-observable Ignored + slot behaviour that the
// guard-clause build() must preserve: a mapping whose target field is not
// emitted, a mapping whose signal has no DBC definition, and a full numeric
// population across every emitted field. Behaviour, not implementation.

TEST_F(VehicleSignalFactoryTest, MappingToNonEmittedField_LeavesFieldIgnoredWithoutError) {
    // Contract #1: a config mapping a signal name to a field the factory does
    // NOT emit (gearRequested). The frame carries the signal, but the output
    // field must stay absent (Ignored) — no error, no invented value. A sibling
    // mapping to a real field still populates, proving build() ran cleanly.
    auto config = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DIR_axleSpeed", "gearRequested"},   // not an emitted field
            {"DI_accelPedalPos", "throttlePercent"} // sibling emitted field
        },
        "",
        false
    );

    DBCParseResult result;
    result.signalsByCanId[264].emplace_back(DBCSignalParams{
        264, "DIR_axleSpeed", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "", 0.0, 100.0
    });
    // DI_accelPedalPos redefined here as Intel 0|8 scale 0.4 so byte0 carries raw.
    result.signalsByCanId[280].emplace_back(DBCSignalParams{
        280, "DI_accelPedalPos", 0, 8, DBCByteOrder::Intel, 0.4, 0.0, false, "%", 0.0, 100.0
    });

    VehicleSignalFactory factory(*config, result);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    // DIR_axleSpeed raw 99 present; DI_accelPedalPos raw 100 -> 40.0 present.
    frames[264] = {0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    frames[280] = {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    auto signal = factory.build(frames, 1);

    // The non-emitted mapping leaves no output slot to assert directly; the
    // observable contract is that build() succeeds and the sibling slot works.
    ASSERT_TRUE(signal.getThrottlePercent().has_value());
    EXPECT_NEAR(signal.getThrottlePercent().value(), 40.0, 0.01);
    // No gear mapping was configured, and the ignored mapping must not invent one.
    EXPECT_FALSE(signal.getGearSelector().has_value());
}

TEST_F(VehicleSignalFactoryTest, MappingWithNoDbcDefinition_LeavesFieldAbsentWithoutError) {
    // Contract #2: a config mapping a signal name that has NO DBC definition in
    // the parse result. build() leaves the output field absent — not a crash,
    // not an invented value. A second mapping that IS defined still populates,
    // proving the missing-definition path is tolerated rather than fatal.
    auto config = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"DI_missingSignal", "throttlePercent"}, // name absent from parseResult
            {"DIR_axleSpeed", "motorRpm"}             // defined sibling
        },
        "",
        false
    );

    DBCParseResult result;
    // NOTE: deliberately no DI_missingSignal entry. DIR_axleSpeed present.
    result.signalsByCanId[264].emplace_back(DBCSignalParams{
        264, "DIR_axleSpeed", 40, 16, DBCByteOrder::Intel, 0.1, 0.0, true, "RPM", -2750.0, 2750.0
    });

    VehicleSignalFactory factory(*config, result);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[264] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00}; // motorRpm raw 25000 -> 2500.0

    auto signal = factory.build(frames, 1);

    EXPECT_FALSE(signal.getThrottlePercent().has_value()); // missing-definition mapping ignored
    ASSERT_TRUE(signal.getMotorRpm().has_value());          // defined sibling unaffected
    EXPECT_EQ(signal.getMotorRpm().value(), 2500.0);
}

TEST_F(VehicleSignalFactoryTest, AllEmittedNumericFields_PopulatedByMatchingFrames) {
    // Contract #3: config mappings covering each numeric field the factory
    // emits, with a frame carrying each signal, populate the corresponding
    // output field in the returned VehicleSignal. Each signal lives on its own
    // CAN id with a simple Intel encoding so the expected physical value is
    // hand-verifiable. This locks the full slot-indexing table end-to-end.
    auto config = std::make_unique<VehicleConfig>(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        std::unordered_map<std::string, std::string>{
            {"s_throttle",      "throttlePercent"},
            {"s_speed",         "speedKmh"},
            {"s_accel",         "accelerationG"},
            {"s_brake",         "brakePercent"},
            {"s_steering",      "steeringAngleDeg"},
            {"s_rpm",           "motorRpm"},
            {"s_hv_voltage",    "motorHvVoltage"},
            {"s_hv_current",    "motorHvCurrent"},
            {"s_torque",        "motorTorqueNm"}
        },
        "",
        false
    );

    DBCParseResult result;
    // Each: Intel, startBit 0, so raw sits in the low bytes directly.
    result.signalsByCanId[601].emplace_back(DBCSignalParams{
        601, "s_throttle", 0, 8, DBCByteOrder::Intel, 0.4, 0.0, false, "%", 0.0, 100.0
    });                                  // raw 100 -> 40.0
    result.signalsByCanId[602].emplace_back(DBCSignalParams{
        602, "s_speed", 0, 12, DBCByteOrder::Intel, 0.1, 0.0, true, "kph", 0.0, 300.0
    });                                  // raw 500 -> 50.0
    result.signalsByCanId[603].emplace_back(DBCSignalParams{
        603, "s_accel", 0, 8, DBCByteOrder::Intel, 0.05, 0.0, false, "g", -5.0, 5.0
    });                                  // raw 10 -> 0.5
    result.signalsByCanId[604].emplace_back(DBCSignalParams{
        604, "s_brake", 0, 8, DBCByteOrder::Intel, 1.0, 0.0, false, "%", 0.0, 100.0
    });                                  // raw 50 -> 50.0
    result.signalsByCanId[605].emplace_back(DBCSignalParams{
        605, "s_steering", 0, 16, DBCByteOrder::Intel, 0.1, -819.2, false, "deg", -819.2, 819.1
    });                                  // raw 0 -> -819.2
    result.signalsByCanId[606].emplace_back(DBCSignalParams{
        606, "s_rpm", 0, 16, DBCByteOrder::Intel, 0.1, 0.0, true, "RPM", 0.0, 20000.0
    });                                  // raw 25000 -> 2500.0
    result.signalsByCanId[607].emplace_back(DBCSignalParams{
        607, "s_hv_voltage", 0, 16, DBCByteOrder::Intel, 0.5, 0.0, false, "V", 0.0, 1000.0
    });                                  // raw 800 -> 400.0
    result.signalsByCanId[608].emplace_back(DBCSignalParams{
        608, "s_hv_current", 0, 8, DBCByteOrder::Intel, 0.5, 0.0, false, "A", 0.0, 50.0
    });                                  // raw 40 -> 20.0
    result.signalsByCanId[609].emplace_back(DBCSignalParams{
        609, "s_torque", 0, 16, DBCByteOrder::Intel, 2.0, 0.0, true, "Nm", -7500.0, 7500.0
    });                                  // raw -1024 (0xFC00) -> -2048.0

    VehicleSignalFactory factory(*config, result);

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> frames;
    frames[601] = {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // throttle 100 -> 40.0
    frames[602] = {0xF4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // speed 500 -> 50.0
    frames[603] = {0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // accel 10 -> 0.5
    frames[604] = {0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // brake 50 -> 50.0
    frames[605] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // steering 0 -> -819.2
    frames[606] = {0xA8, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // rpm 25000 -> 2500.0
    frames[607] = {0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // hv_v 800 -> 400.0
    frames[608] = {0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // hv_c 40 -> 20.0
    frames[609] = {0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // torque -1024 -> -2048.0

    auto signal = factory.build(frames, 1);

    ASSERT_TRUE(signal.getThrottlePercent().has_value());
    EXPECT_NEAR(signal.getThrottlePercent().value(), 40.0, 0.01);
    ASSERT_TRUE(signal.getSpeedKmh().has_value());
    EXPECT_NEAR(signal.getSpeedKmh().value(), 50.0, 0.1);
    ASSERT_TRUE(signal.getAccelerationG().has_value());
    EXPECT_NEAR(signal.getAccelerationG().value(), 0.5, 0.001);
    ASSERT_TRUE(signal.getBrakePercent().has_value());
    EXPECT_NEAR(signal.getBrakePercent().value(), 50.0, 0.01);
    ASSERT_TRUE(signal.getSteeringAngleDeg().has_value());
    EXPECT_NEAR(signal.getSteeringAngleDeg().value(), -819.2, 0.01);
    ASSERT_TRUE(signal.getMotorRpm().has_value());
    EXPECT_NEAR(signal.getMotorRpm().value(), 2500.0, 0.1);
    ASSERT_TRUE(signal.getMotorHvVoltage().has_value());
    EXPECT_NEAR(signal.getMotorHvVoltage().value(), 400.0, 0.1);
    ASSERT_TRUE(signal.getMotorHvCurrent().has_value());
    EXPECT_NEAR(signal.getMotorHvCurrent().value(), 20.0, 0.01);
    ASSERT_TRUE(signal.getMotorTorqueNm().has_value());
    EXPECT_NEAR(signal.getMotorTorqueNm().value(), -2048.0, 0.1);
}