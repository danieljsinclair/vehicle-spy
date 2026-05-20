#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <algorithm>
#include "vehicle-sim/domain/DBCParser.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"
#include "vehicle-sim/domain/DBCSignalMapper.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::domain;

// ================================================
// DBC Signal Definition Struct Tests
// Tests that the signal definition value object can be constructed
// ================================================

TEST(DBCSignalDefinitionTest, ConstructsWithAllRequiredFields) {
    const DBCSignalDefinition def(
        264,
        "DIR_axleSpeed",
        40,
        16,
        DBCByteOrder::Intel,
        0.1,
        0.0,
        true,
        "RPM",
        -2750.0,
        2750.0
    );

    EXPECT_EQ(def.canId, 264);
    EXPECT_EQ(def.name, "DIR_axleSpeed");
    EXPECT_EQ(def.startBit, 40);
    EXPECT_EQ(def.bitLength, 16);
    EXPECT_EQ(def.byteOrder, DBCByteOrder::Intel);
    EXPECT_DOUBLE_EQ(def.scale, 0.1);
    EXPECT_DOUBLE_EQ(def.offset, 0.0);
    EXPECT_TRUE(def.isSigned);
    EXPECT_EQ(def.unit, "RPM");
}

TEST(DBCSignalDefinitionTest, ConstructsUnsignedSignal) {
    const DBCSignalDefinition def(
        280,
        "DI_accelPedalPos",
        32,
        8,
        DBCByteOrder::Intel,
        0.4,
        0.0,
        false,
        "%",
        0.0,
        100.0
    );

    EXPECT_EQ(def.name, "DI_accelPedalPos");
    EXPECT_EQ(def.canId, 280);
    EXPECT_FALSE(def.isSigned);
}

TEST(DBCSignalDefinitionTest, ConstructsSignalWithOffset) {
    const DBCSignalDefinition def(
        297,
        "SteeringAngle129",
        16,
        14,
        DBCByteOrder::Intel,
        0.1,
        -819.2,
        false,
        "deg",
        -819.2,
        819.1
    );

    EXPECT_DOUBLE_EQ(def.offset, -819.2);
}

TEST(DBCSignalDefinitionTest, ConstructsIntelByteOrderSignal) {
    const DBCSignalDefinition def(
        264,
        "DIR_torqueActual",
        27,
        13,
        DBCByteOrder::Intel,
        2.0,
        0.0,
        true,
        "Nm",
        -7500.0,
        7500.0
    );

    EXPECT_EQ(def.byteOrder, DBCByteOrder::Intel);
}

TEST(DBCSignalDefinitionTest, EqualityWorks) {
    const DBCSignalDefinition def1(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    const DBCSignalDefinition def2(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    EXPECT_EQ(def1, def2);
}

TEST(DBCSignalDefinitionTest, InequalityWorks) {
    const DBCSignalDefinition def1(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    const DBCSignalDefinition def2(
        264, "DIR_torqueActual", 27, 13,
        DBCByteOrder::Intel, 2.0, 0.0, true, "Nm",
        -7500.0, 7500.0
    );

    EXPECT_NE(def1, def2);
}

// ================================================
// DBC Parse Result Tests
// Tests organizing signal definitions by CAN ID
// ================================================

TEST(DBCParseResultTest, EmptyResultHasZeroSignals) {
    DBCParseResult result;

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST(DBCParseResultTest, ReturnsNullptrForUnknownCanId) {
    DBCParseResult result;

    const auto* signals = result.getSignalsForCanId(264);

    EXPECT_EQ(signals, nullptr);
}

// ================================================
// Signal Mapper Tests
// Tests extracting signals using DBC definitions
// Uses VERIFIED byte patterns from research docs
// ================================================

class DBCSignalMapperTest : public ::testing::Test {
protected:
    std::vector<uint8_t> canFrame{0, 0, 0, 0, 0, 0, 0, 0};
};

TEST_F(DBCSignalMapperTest, ExtractsDIR_axleSpeedFromKnownBytes) {
    // DIR_axleSpeed: bit 40, 16-bit signed, scale 0.1, Intel order (@1-)
    // Source: tesla-drive-motor-signals.md, CAN 264 (0x108)
    // Example: 1000 RPM = raw 10000 = 0x2710
    // Intel: LSB at lower byte → byte 5 = 0x10, byte 6 = 0x27
    canFrame[5] = 0x10;
    canFrame[6] = 0x27;

    const DBCSignalDefinition def(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1000.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDIR_axleSpeedZeroRPM) {
    // Zero RPM: raw 0
    canFrame[5] = 0x00;
    canFrame[6] = 0x00;

    const DBCSignalDefinition def(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDIR_axleSpeedNegative) {
    // Negative speed (regen): -500 RPM = raw -5000
    // Two's complement of -5000 (16-bit): 0xEC18
    // Intel: byte 5 = 0x18 (LSB), byte 6 = 0xEC (MSB)
    // Verify: 0xEC18 = 60440. As signed 16-bit: 60440 - 65536 = -5096. -5096 * 0.1 = -509.6
    // That's wrong! Raw -5000 in 16-bit two's complement:
    // 65536 - 5000 = 60536 = 0xEC78
    // Intel: byte 5 = 0x78, byte 6 = 0xEC
    canFrame[5] = 0x78;
    canFrame[6] = 0xEC;

    const DBCSignalDefinition def(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, -500.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDIR_torqueActualFromKnownBytes) {
    // DIR_torqueActual: bit 27, 13-bit signed, scale 2, Intel order (@1-)
    // Source: tesla-drive-motor-signals.md, CAN 264 (0x108)
    // Example: 100 Nm = raw 50 (100 / 2 = 50)
    // Intel extraction: raw bit 0 = DBC bit 27 = byte 3 bit 3
    // Raw 50 = 0b0000000110010 (13-bit)
    // bits 0-4 → byte 3 bits 3-7: 0b10010 = bit 3,4 set → 0x18
    // bits 5-12 → byte 4 bits 0-7: 0b0000001 → 0x01
    // Actually: raw 50 = 0b000000110010
    // bit 0: 0, bit 1: 1, bit 2: 0, bit 3: 0, bit 4: 1, bit 5: 1
    // bits 0-4 in byte 3 at bit 3: bits 3,4 set → 0x18
    // bits 5-12 in byte 4: bit 0,1 set → 0x03... wait let me compute properly
    // raw 50 = binary 110010 → bit1=1, bit4=1, bit5=1
    // Intel: bit 0→DBC 27(byte3 bit3)=0, bit1→DBC28(byte3 bit4)=1, bit2→DBC29(byte3 bit5)=0,
    //        bit3→DBC30(byte3 bit6)=0, bit4→DBC31(byte3 bit7)=1,
    //        bit5→DBC32(byte4 bit0)=1, rest=0
    // byte 3: bits 4,7 set → 0x90? No. bits at positions 4 and 7 → 0x90
    // Actually: bit4 means bit index 4 in byte 3, so byte3 |= (1<<4), byte3 |= (1<<7)
    // byte 3 = 0b10010000 = 0x90? Wait, (1<<4)=0x10, (1<<7)=0x80, sum=0x90
    // byte 4: bit0 set → 0x01
    canFrame[3] = 0x90;
    canFrame[4] = 0x01;

    const DBCSignalDefinition def(
        264, "DIR_torqueActual", 27, 13,
        DBCByteOrder::Intel, 2.0, 0.0, true, "Nm",
        -7500.0, 7500.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 100.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDIR_torqueActualZero) {
    // Zero torque: raw 0
    canFrame[3] = 0x00;
    canFrame[4] = 0x00;

    const DBCSignalDefinition def(
        264, "DIR_torqueActual", 27, 13,
        DBCByteOrder::Intel, 2.0, 0.0, true, "Nm",
        -7500.0, 7500.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDIR_torqueActualNegativeRegen) {
    // Negative torque (regen): -150 Nm = raw -75
    // Two's complement of -75 (13-bit): 0b1_1111_1111_011010
    // = 8192 - 75 = 8117 = 0x1FxB5... let me compute properly
    // 13-bit two's complement: (~75 + 1) & 0x1FFF = (~75 + 1) & 8191
    // 75 = 0b0000001001011, ~75 = 0b1111110110100 (13-bit), +1 = 0b1111110110101
    // = 8117 = 0x1FB5
    // Intel: raw bits 0-12 map to DBC bits 27-39
    // 8117 = 0b1_1111_1011_0101
    // bit0=1→byte3 bit3, bit2=1→byte3 bit5, bit4=1→byte3 bit7,
    // bit5=1→byte4 bit0, bit6=1→byte4 bit1, bit7=0→, bit8=1→byte4 bit3,
    // bit9=1→byte4 bit4, bit10=1→byte4 bit5, bit11=1→byte4 bit6, bit12=1→byte4 bit7
    // byte3: bits 3,5,7 set = 0xA8
    // byte4: bits 0,2,3,4,5,6,7 set = 0xFD
    canFrame[3] = 0xA8;
    canFrame[4] = 0xFD;

    const DBCSignalDefinition def(
        264, "DIR_torqueActual", 27, 13,
        DBCByteOrder::Intel, 2.0, 0.0, true, "Nm",
        -7500.0, 7500.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, -150.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDI_accelPedalPosFromKnownBytes) {
    // DI_accelPedalPos: bit 32, 8-bit unsigned, scale 0.4, Motorola order (@1+)
    // Source: can-signal-reference.md, CAN 280 (0x118)
    // Example: 50% throttle = raw 125 (125 * 0.4 = 50.0)
    // Bit 32 = byte 4
    canFrame[4] = 125;

    const DBCSignalDefinition def(
        280, "DI_accelPedalPos", 32, 8,
        DBCByteOrder::Intel, 0.4, 0.0, false, "%",
        0.0, 100.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 50.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDI_accelPedalPosFullThrottle) {
    // Full throttle: 100% = raw 250 (250 * 0.4 = 100.0)
    canFrame[4] = 250;

    const DBCSignalDefinition def(
        280, "DI_accelPedalPos", 32, 8,
        DBCByteOrder::Intel, 0.4, 0.0, false, "%",
        0.0, 100.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 100.0);
}

TEST_F(DBCSignalMapperTest, ExtractsDI_accelPedalPosZero) {
    // Zero throttle: 0% = raw 0
    canFrame[4] = 0;

    const DBCSignalDefinition def(
        280, "DI_accelPedalPos", 32, 8,
        DBCByteOrder::Intel, 0.4, 0.0, false, "%",
        0.0, 100.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST_F(DBCSignalMapperTest, ExtractsSCCM_steeringAngleFromKnownBytes) {
    // SCCM_steeringAngle: bit 16, 14-bit, scale 0.1, offset -819.2, Motorola (@1+)
    // Source: can-signal-reference.md, CAN 297 (0x129)
    // Example: 0 deg center = raw 8192 = 0x2000
    // Byte 2 = 0x00, Byte 3 = 0x20 (14-bit from bit 16)
    canFrame[2] = 0x00;
    canFrame[3] = 0x20;

    const DBCSignalDefinition def(
        297, "SteeringAngle129", 16, 14,
        DBCByteOrder::Intel, 0.1, -819.2, false, "deg",
        -819.2, 819.1
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.0, 0.1);
}

TEST_F(DBCSignalMapperTest, ExtractsSCCM_steeringAnglePositive) {
    // 90 deg right: raw = (90 + 819.2) / 0.1 = 9092
    // 9092 = 0x2384
    // Intel: byte 2 (bits 16-23) = 0x84, byte 3 (bits 24-29) = 0x23 & 0x3F = 0x23
    canFrame[2] = 0x84;
    canFrame[3] = 0x23;

    const DBCSignalDefinition def(
        297, "SteeringAngle129", 16, 14,
        DBCByteOrder::Intel, 0.1, -819.2, false, "deg",
        -819.2, 819.1
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 90.0, 0.1);
}

TEST_F(DBCSignalMapperTest, ExtractsSCCM_steeringAngleNegative) {
    // -90 deg left: raw = (-90 + 819.2) / 0.1 = 7292
    // 7292 = 0x1C7C, 14-bit from bit 16
    // Byte 2 = 0x7C, Byte 3 = 0x1C
    canFrame[2] = 0x7C;
    canFrame[3] = 0x1C;

    const DBCSignalDefinition def(
        297, "SteeringAngle129", 16, 14,
        DBCByteOrder::Intel, 0.1, -819.2, false, "deg",
        -819.2, 819.1
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -90.0, 0.1);
}

TEST_F(DBCSignalMapperTest, ReturnsNulloptForFrameTooShort) {
    std::vector<uint8_t> shortFrame{0, 0, 0};

    const DBCSignalDefinition def(
        264, "DIR_axleSpeed", 40, 16,
        DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
        -2750.0, 2750.0
    );

    auto result = DBCSignalMapper::mapSignal(shortFrame, def);

    EXPECT_FALSE(result.has_value());
}

TEST_F(DBCSignalMapperTest, Extracts1BitBooleanSignal) {
    // DI_brakePedalState: bit 19, 2-bit unsigned
    // bit 19 = byte 2 bit 3. Value 1 = ON.
    // Set byte 2 bit 3: 0x08
    canFrame[2] = 0x08;

    const DBCSignalDefinition def(
        280, "DI_brakePedalState", 19, 2,
        DBCByteOrder::Intel, 1.0, 0.0, false, "",
        0.0, 2.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1.0);
}

// ================================================
// Motorola Byte Order Decode Tests
// Verified using DBC convention: bit n → byte=n/8, bit_within_byte=7-(n%8)
// MSB at startBit, LSB at startBit+bitLength-1
// ================================================

TEST_F(DBCSignalMapperTest, ExtractsMotorola8BitSignalAtBit7) {
    // 8-bit Motorola at bit 7, scale 1, offset 0
    // DBC bit 7 = byte 0 bit 0 (MSB of signal)
    // DBC bits 8-14 = byte 1 bits 7,6,5,4,3,2,1
    // Value 0xAB = 10101011:
    //   bit7(result) → byte0 bit0 = 1 → frame[0] = 0x01
    //   bit6(result) → byte1 bit7 = 0
    //   bit5(result) → byte1 bit6 = 1 → frame[1] |= 0x40
    //   bit4(result) → byte1 bit5 = 0
    //   bit3(result) → byte1 bit4 = 1 → frame[1] |= 0x10
    //   bit2(result) → byte1 bit3 = 0
    //   bit1(result) → byte1 bit2 = 1 → frame[1] |= 0x04
    //   bit0(result) → byte1 bit1 = 1 → frame[1] |= 0x02
    // frame[0] = 0x01, frame[1] = 0x56
    canFrame[0] = 0x01;
    canFrame[1] = 0x56;

    const DBCSignalDefinition def(
        100, "MotoTest", 7, 8,
        DBCByteOrder::Motorola, 1.0, 0.0, false, "", 0.0, 255.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 171.0);  // 0xAB
}

TEST_F(DBCSignalMapperTest, ExtractsMotorola4BitSignalWithinByte) {
    // 4-bit Motorola at bit 3, scale 1, offset 0
    // DBC bit 3 = byte 0 bit 4 (MSB of signal)
    // DBC bit 4 = byte 0 bit 3
    // DBC bit 5 = byte 0 bit 2
    // DBC bit 6 = byte 0 bit 1
    // Value 0xA = 1010:
    //   bit3(result) → byte0 bit4 = 1 → 0x10
    //   bit2(result) → byte0 bit3 = 0
    //   bit1(result) → byte0 bit2 = 1 → 0x04
    //   bit0(result) → byte0 bit1 = 0
    // frame[0] = 0x14
    canFrame[0] = 0x14;

    const DBCSignalDefinition def(
        100, "MotoTest", 3, 4,
        DBCByteOrder::Motorola, 1.0, 0.0, false, "", 0.0, 15.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 10.0);  // 0xA
}

TEST_F(DBCSignalMapperTest, ExtractsMotorola16BitSpanningTwoBytes) {
    // 16-bit Motorola at bit 7, scale 1, offset 0
    // Value 0x1234 = 4660
    // Computer-verified byte pattern: frame[1]=0x24, frame[2]=0x68
    canFrame[0] = 0x00;
    canFrame[1] = 0x24;
    canFrame[2] = 0x68;

    const DBCSignalDefinition def(
        100, "MotoTest", 7, 16,
        DBCByteOrder::Motorola, 1.0, 0.0, false, "", 0.0, 65535.0
    );

    auto result = DBCSignalMapper::mapSignal(canFrame, def);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 4660.0);  // 0x1234
}

TEST_F(DBCSignalMapperTest, MapsSignalByCanIdAndName) {
    // Setup signal definitions map
    std::unordered_map<uint16_t, std::vector<DBCSignalDefinition>> definitions;
    definitions[264].push_back(
        DBCSignalDefinition(264, "DIR_axleSpeed", 40, 16,
            DBCByteOrder::Intel, 0.1, 0.0, true, "RPM",
            -2750.0, 2750.0)
    );

    // Frame with 1000 RPM (Intel byte order)
    canFrame[5] = 0x10;
    canFrame[6] = 0x27;

    auto result = DBCSignalMapper::mapSignal(canFrame, 264, "DIR_axleSpeed", definitions);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(*result, 1000.0);
}

// ================================================
// Vehicle Config Tests
// Tests mapping DBC signal names to VehicleSignal fields
// ================================================

TEST(VehicleConfigTest, ConstructsMinimalConfig) {
    VehicleConfig config(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {
            {"DIR_axleSpeed", "motorRpm"},
            {"DI_accelPedalPos", "throttlePercent"}
        }
    );

    EXPECT_EQ(config.dbcFilePath, "tesla_model3.dbc");
    EXPECT_EQ(config.vehicleName, "Tesla Model Y");
    EXPECT_TRUE(config.hasMapping("DIR_axleSpeed"));
    EXPECT_TRUE(config.hasMapping("DI_accelPedalPos"));
}

TEST(VehicleConfigTest, GetsFieldForSignal) {
    VehicleConfig config(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {
            {"DIR_axleSpeed", "motorRpm"},
            {"DI_accelPedalPos", "throttlePercent"}
        }
    );

    EXPECT_EQ(config.getFieldForSignal("DIR_axleSpeed"), "motorRpm");
    EXPECT_EQ(config.getFieldForSignal("DI_accelPedalPos"), "throttlePercent");
    EXPECT_EQ(config.getFieldForSignal("UnknownSignal"), "");
}

TEST(VehicleConfigTest, HasMappingWorks) {
    VehicleConfig config(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {
            {"DIR_axleSpeed", "motorRpm"}
        }
    );

    EXPECT_TRUE(config.hasMapping("DIR_axleSpeed"));
    EXPECT_FALSE(config.hasMapping("UnknownSignal"));
}

TEST(VehicleConfigTest, EqualityWorks) {
    VehicleConfig config1(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {{"DIR_axleSpeed", "motorRpm"}}
    );

    VehicleConfig config2(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {{"DIR_axleSpeed", "motorRpm"}}
    );

    EXPECT_EQ(config1, config2);
}

TEST(VehicleConfigTest, InequalityWorks) {
    VehicleConfig config1(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {{"DIR_axleSpeed", "motorRpm"}}
    );

    VehicleConfig config2(
        "audi_etron.dbc",
        "audi_etron.dbc",
        "Audi e-tron",
        {{"ESP_v_Signal", "speedKmh"}}
    );

    EXPECT_NE(config1, config2);
}

// ================================================
// Vehicle Config Registry Tests
// Tests managing multiple vehicle configs
// ================================================

class VehicleConfigRegistryTest : public ::testing::Test {
protected:
    VehicleConfigRegistry registry;
};

TEST_F(VehicleConfigRegistryTest, RegistersAndRetrievesConfig) {
    VehicleConfig teslaConfig(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {{"DIR_axleSpeed", "motorRpm"}}
    );

    registry.registerVehicle("tesla_model_y", teslaConfig);

    const auto* retrieved = registry.getConfig("tesla_model_y");

    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->vehicleName, "Tesla Model Y");
    EXPECT_TRUE(retrieved->hasMapping("DIR_axleSpeed"));
}

TEST_F(VehicleConfigRegistryTest, ReturnsNullptrForUnknownVehicle) {
    const auto* config = registry.getConfig("unknown_vehicle");

    EXPECT_EQ(config, nullptr);
}

TEST_F(VehicleConfigRegistryTest, HasConfigWorks) {
    VehicleConfig config(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {}
    );

    registry.registerVehicle("tesla_model_y", config);

    EXPECT_TRUE(registry.hasConfig("tesla_model_y"));
    EXPECT_FALSE(registry.hasConfig("unknown_vehicle"));
}

TEST_F(VehicleConfigRegistryTest, ReturnsRegisteredVehicleIds) {
    registry.registerVehicle("tesla_model_y",
        VehicleConfig("tesla_model3.dbc", "tesla_model3.dbc", "Tesla Model Y", {}));
    registry.registerVehicle("audi_etron",
        VehicleConfig("vw_mlb.dbc", "vw_mlb.dbc", "Audi e-tron", {}));

    auto vehicleIds = registry.getRegisteredVehicles();

    EXPECT_EQ(vehicleIds.size(), 2);
    EXPECT_NE(std::find(vehicleIds.begin(), vehicleIds.end(), "tesla_model_y"), vehicleIds.end());
    EXPECT_NE(std::find(vehicleIds.begin(), vehicleIds.end(), "audi_etron"), vehicleIds.end());
}

TEST_F(VehicleConfigRegistryTest, MultipleConfigsCanCoexist) {
    registry.registerVehicle("tesla_model_y",
        VehicleConfig("tesla_model3.dbc", "tesla_model3.dbc", "Tesla Model Y",
            {{"DIR_axleSpeed", "motorRpm"}}));

    registry.registerVehicle("audi_etron",
        VehicleConfig("vw_mlb.dbc", "vw_mlb.dbc", "Audi e-tron",
            {{"ESP_v_Signal", "speedKmh"}}));

    const auto* tesla = registry.getConfig("tesla_model_y");
    const auto* audi = registry.getConfig("audi_etron");

    ASSERT_NE(tesla, nullptr);
    ASSERT_NE(audi, nullptr);

    EXPECT_TRUE(tesla->hasMapping("DIR_axleSpeed"));
    EXPECT_FALSE(tesla->hasMapping("ESP_v_Signal"));

    EXPECT_TRUE(audi->hasMapping("ESP_v_Signal"));
    EXPECT_FALSE(audi->hasMapping("DIR_axleSpeed"));
}

TEST_F(VehicleConfigRegistryTest, RegistersMultipleSignals) {
    VehicleConfig config(
        "tesla_model3.dbc",
        "tesla_model3.dbc",
        "Tesla Model Y",
        {
            {"DIR_axleSpeed", "motorRpm"},
            {"DIR_torqueActual", "motorTorque"},
            {"DI_accelPedalPos", "throttlePercent"},
            {"SteeringAngle129", "steeringAngleDeg"}
        }
    );

    registry.registerVehicle("tesla_model_y", config);

    const auto* retrieved = registry.getConfig("tesla_model_y");

    ASSERT_NE(retrieved, nullptr);
    EXPECT_TRUE(retrieved->hasMapping("DIR_axleSpeed"));
    EXPECT_TRUE(retrieved->hasMapping("DIR_torqueActual"));
    EXPECT_TRUE(retrieved->hasMapping("DI_accelPedalPos"));
    EXPECT_TRUE(retrieved->hasMapping("SteeringAngle129"));
}
