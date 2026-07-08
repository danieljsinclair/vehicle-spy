#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"
#include "vehicle-sim/domain/DBCSignalMapper.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

// =============================================================================
// SRP refactor safety net for DBCSignalMapper + DBCFileParser.
//
// These tests lock the parse/decode CONTRACT from the documented behaviour
// (headers + Gear.h canonical mapping), NOT by reading the implementation.
// They exist so the high cognitive-complexity functions in DBCFileParser.cpp
// and DBCSignalMapper.cpp can be decomposed without silently changing the
// external contract. Every case here must be GREEN against current code.
// =============================================================================

namespace {

// Canonical Tesla DI_gear signal: startBit 21, 3 bits, Intel, unsigned.
// The bit-extraction contract is already locked elsewhere (see IntelShort3Bit
// cases in DBCParser.test.cpp): byte[2]=0x32 -> raw 1, 0x55 -> raw 2,
// 0x95 -> raw 4. We rely on that locked contract here, not on the mapper impl.
DBCSignalDefinition makeGearSignal() {
    return DBCSignalDefinition(DBCSignalParams{
        280, "DI_gear", 21, 3,
        DBCByteOrder::Intel, 1.0, 0.0, false, "",
        0.0, 7.0,
        std::vector<DBCValueEntry>{
            {0, "INVALID"}, {1, "DI_GEAR_P"}, {2, "DI_GEAR_R"},
            {3, "DI_GEAR_N"}, {4, "DI_GEAR_D"}, {7, "DI_GEAR_SNA"}
        }
    });
}

std::unordered_map<std::uint16_t, std::vector<DBCSignalDefinition>>
makeGearDefinitions() {
    std::unordered_map<std::uint16_t, std::vector<DBCSignalDefinition>> defs;
    defs[280].push_back(makeGearSignal());
    return defs;
}

// Encode a 3-bit raw gear value into byte[2] bits 5-7 (Intel start 21).
std::vector<std::uint8_t> gearFrame(std::uint8_t raw) {
    std::vector<std::uint8_t> frame(8, 0);
    frame[2] = static_cast<std::uint8_t>(raw << 5);
    return frame;
}

} // namespace

// =============================================================================
// DBCSignalMapper::mapGearSignal — the canonical Tesla gear mapping.
// Doc contract (DBCSignalMapper.h / Gear.h):
//   raw 1 -> PARK (-2), raw 2 -> REVERSE (-1), raw 3 -> NEUTRAL (0),
//   raw 4 -> AUTO_1 (0x1001), raw 0 -> nullopt, raw 7 (SNA) -> nullopt.
// Currently has ZERO test coverage despite being used in production.
// =============================================================================

class DBCGearMappingTest : public ::testing::Test {};

TEST_F(DBCGearMappingTest, GearPark_DecodesToCanonicalPARK) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(1), 280, "DI_gear", makeGearDefinitions());
    ASSERT_TRUE(gear.has_value());
    EXPECT_EQ(*gear, Gear::PARK);
}

TEST_F(DBCGearMappingTest, GearReverse_DecodesToCanonicalREVERSE) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(2), 280, "DI_gear", makeGearDefinitions());
    ASSERT_TRUE(gear.has_value());
    EXPECT_EQ(*gear, Gear::REVERSE);
}

TEST_F(DBCGearMappingTest, GearNeutral_DecodesToCanonicalNEUTRAL) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(3), 280, "DI_gear", makeGearDefinitions());
    ASSERT_TRUE(gear.has_value());
    EXPECT_EQ(*gear, Gear::NEUTRAL);
}

TEST_F(DBCGearMappingTest, GearDrive_DecodesToCanonicalAUTO_1) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(4), 280, "DI_gear", makeGearDefinitions());
    ASSERT_TRUE(gear.has_value());
    EXPECT_EQ(*gear, Gear::AUTO_1);
}

TEST_F(DBCGearMappingTest, GearInvalidZero_ReturnsNullopt) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(0), 280, "DI_gear", makeGearDefinitions());
    EXPECT_FALSE(gear.has_value());
}

TEST_F(DBCGearMappingTest, GearSNA_ReturnsNullopt) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(7), 280, "DI_gear", makeGearDefinitions());
    EXPECT_FALSE(gear.has_value());
}

TEST_F(DBCGearMappingTest, UnknownCanId_ReturnsNullopt) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(1), 999, "DI_gear", makeGearDefinitions());
    EXPECT_FALSE(gear.has_value());
}

TEST_F(DBCGearMappingTest, UnknownSignalName_ReturnsNullopt) {
    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(1), 280, "does_not_exist", makeGearDefinitions());
    EXPECT_FALSE(gear.has_value());
}

// =============================================================================
// DBCSignalMapper::mapSignal (canId + name overload) lookup-failure paths.
// Locks the contract that a missing ID or missing name yields nullopt rather
// than an exception or zero. (Extraction success is locked elsewhere.)
// =============================================================================

class DBCSignalLookupTest : public ::testing::Test {};

TEST_F(DBCSignalLookupTest, ByName_UnknownCanId_ReturnsNullopt) {
    const auto defs = makeGearDefinitions();
    std::vector<std::uint8_t> frame(8, 0);
    auto v = DBCSignalMapper::mapSignal(frame, 9999, "DI_gear", defs);
    EXPECT_FALSE(v.has_value());
}

TEST_F(DBCSignalLookupTest, ByName_UnknownSignal_ReturnsNullopt) {
    const auto defs = makeGearDefinitions();
    std::vector<std::uint8_t> frame(8, 0);
    auto v = DBCSignalMapper::mapSignal(frame, 280, "MissingSignal", defs);
    EXPECT_FALSE(v.has_value());
}

TEST_F(DBCSignalLookupTest, ByName_EmptyDefinitions_ReturnsNullopt) {
    std::unordered_map<std::uint16_t, std::vector<DBCSignalDefinition>> empty;
    std::vector<std::uint8_t> frame(8, 0);
    auto v = DBCSignalMapper::mapSignal(frame, 280, "DI_gear", empty);
    EXPECT_FALSE(v.has_value());
}

// =============================================================================
// DBCSignalMapper: signed Motorola signal crossing a byte boundary.
// Locks the two's-complement decode for a big-endian signal that spans bytes,
// distinct from the unsigned Motorola spanning case already covered.
// =============================================================================

TEST(DBCSignalMapperSignedMotorolaTest, NegativeValue_DecodesAcrossBytes) {
    // 16-bit Motorola at startBit 7 (MSB = byte 0 bit 7), scale 1, offset 0,
    // signed. A negative raw value is encoded in big-endian two's complement
    // across byte 0 (MSB) and byte 1. Raw -1 -> 0xFFFF -> bytes 0xFF 0xFF.
    std::vector<std::uint8_t> frame(8, 0);
    frame[0] = 0xFF;
    frame[1] = 0xFF;

    const DBCSignalDefinition def(DBCSignalParams{
        100, "SignedMoto", 7, 16,
        DBCByteOrder::Motorola, 1.0, 0.0, true, "",
        -32768.0, 32767.0
    });

    auto v = DBCSignalMapper::mapSignal(frame, def);
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, -1.0);
}

// =============================================================================
// DBCSignalMapper::mapSignal — clamp-to-range contract.
// Locks that the physical value (raw * scale + offset) is clamped to the
// definition's [min, max] rather than returned verbatim when it falls outside.
// These guard the S3776 refactor of the clamp branch in mapSignal.
// =============================================================================

TEST(DBCSignalMapperClampTest, PhysicalValueAboveMax_ReturnsMaxExactly) {
    // Unsigned Intel 8-bit at startBit 0 (byte 0 = raw), scale 1, offset 0.
    // Raw 200 -> physical 200, which exceeds max 100: the result must be the
    // clamped max, not the over-range raw value.
    std::vector<std::uint8_t> frame(8, 0);
    frame[0] = 200;

    const DBCSignalDefinition def(DBCSignalParams{
        100, "ClampedHigh", 0, 8,
        DBCByteOrder::Intel, 1.0, 0.0, false, "",
        0.0, 100.0
    });

    auto v = DBCSignalMapper::mapSignal(frame, def);
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 100.0);
}

TEST(DBCSignalMapperClampTest, PhysicalValueBelowMin_ReturnsMinExactly) {
    // Unsigned Intel 8-bit at startBit 0 (byte 0 = raw), scale 1, offset 0.
    // Raw 5 -> physical 5, which is below min 10: the result must be the
    // clamped min, not the under-range raw value.
    std::vector<std::uint8_t> frame(8, 0);
    frame[0] = 5;

    const DBCSignalDefinition def(DBCSignalParams{
        100, "ClampedLow", 0, 8,
        DBCByteOrder::Intel, 1.0, 0.0, false, "",
        10.0, 100.0
    });

    auto v = DBCSignalMapper::mapSignal(frame, def);
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 10.0);
}

// =============================================================================
// DBCSignalMapper::mapGearSignal — value-table-absent and unknown-raw paths.
// Locks the two remaining gear branches that the S3776 refactor could alter:
// the empty-value-table fallback (raw returned as int32) and the unknown-raw
// path (in range but no matching table entry -> nullopt). Both use the locked
// Intel 21|3 byte[2] encoding (raw n -> frame[2] = n << 5).
// =============================================================================

TEST_F(DBCGearMappingTest, EmptyValueTable_NonZeroNonSevenRaw_ReturnsRawAsInt32) {
    // A gear signal defined WITHOUT a VAL_ table, with a raw value that is
    // neither 0 (INVALID) nor 7 (SNA): the raw value is returned verbatim as
    // an int32 (the direct-mapping fallback).
    std::unordered_map<std::uint16_t, std::vector<DBCSignalDefinition>> defs;
    defs[280].push_back(DBCSignalDefinition(DBCSignalParams{
        280, "DI_gear", 21, 3,
        DBCByteOrder::Intel, 1.0, 0.0, false, "",
        0.0, 7.0,
        {}  // no value table
    }));

    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(5), 280, "DI_gear", defs);
    ASSERT_TRUE(gear.has_value());
    EXPECT_EQ(*gear, static_cast<std::int32_t>(5));
}

TEST_F(DBCGearMappingTest, ValueTableMissingEntry_UnknownRaw_ReturnsNullopt) {
    // A gear signal whose raw value (5) is neither 0 nor 7 and matches no
    // entry in its value table (table defines 1,2,3,4 only): the result is
    // nullopt rather than an invented or default gear.
    std::unordered_map<std::uint16_t, std::vector<DBCSignalDefinition>> defs;
    defs[280].push_back(DBCSignalDefinition(DBCSignalParams{
        280, "DI_gear", 21, 3,
        DBCByteOrder::Intel, 1.0, 0.0, false, "",
        0.0, 7.0,
        std::vector<DBCValueEntry>{
            {1, "DI_GEAR_P"}, {2, "DI_GEAR_R"},
            {3, "DI_GEAR_N"}, {4, "DI_GEAR_D"}
        }
    }));

    auto gear = DBCSignalMapper::mapGearSignal(
        gearFrame(5), 280, "DI_gear", defs);
    EXPECT_FALSE(gear.has_value());
}

// =============================================================================
// DBCFileParser: definition-preserving parse cases not locked elsewhere.
// =============================================================================

class DBCFileParserContractTest : public ::testing::Test {
protected:
    DBCFileParser parser;
};

TEST_F(DBCFileParserContractTest, EmptyUnitString_ParsesAsEmpty) {
    // Signal with no unit (empty quotes) must parse unit as "" rather than
    // dropping the signal or defaulting.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ NoUnit : 0|8@1+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);
    EXPECT_TRUE(signals->at(0).unit.empty());
}

TEST_F(DBCFileParserContractTest, StartBitBeyondOneByte_PreservedAcrossBoundary) {
    // A signal whose definition sits across a byte boundary (startBit 60,
    // 4 bits, Intel) must preserve startBit/bitLength verbatim in the
    // definition so the mapper can later place it in byte 7.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ HiBits : 60|4@1+ (1,0) [0|15] "" ECU
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);
    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.startBit, 60);
    EXPECT_EQ(sig.bitLength, 4);
    EXPECT_EQ(sig.byteOrder, DBCByteOrder::Intel);
}

TEST_F(DBCFileParserContractTest, MultipleValueTables_AppliedToCorrectSignals) {
    // Two distinct signals, each with its own VAL_ table. The parser must
    // attach each table to the matching signal by name (not by order).
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ Gear : 0|3@1+ (1,0) [0|7] "" ECU
 SG_ Door : 8|2@1+ (1,0) [0|3] "" ECU
VAL_ 100 Gear 0 "Park" 1 "Reverse" ;
VAL_ 100 Door 0 "closed" 1 "open" ;
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 2);

    const auto& gear = (signals->at(0).name == "Gear") ? signals->at(0) : signals->at(1);
    const auto& door = (signals->at(0).name == "Door") ? signals->at(0) : signals->at(1);

    ASSERT_EQ(gear.valueTable.size(), 2);
    EXPECT_EQ(gear.valueTable[0].value, 0);
    EXPECT_EQ(gear.valueTable[0].description, "Park");
    EXPECT_EQ(gear.valueTable[1].value, 1);
    EXPECT_EQ(gear.valueTable[1].description, "Reverse");

    ASSERT_EQ(door.valueTable.size(), 2);
    EXPECT_EQ(door.valueTable[0].description, "closed");
    EXPECT_EQ(door.valueTable[1].description, "open");
}

TEST_F(DBCFileParserContractTest, ValueTableWithoutSignal_LeavesResultUnchanged) {
    // A VAL_ referencing an unknown signal must not crash and must not invent
    // a signal; valid signals on the same CAN ID are still parsed.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ Real : 0|8@1+ (1,0) [0|255] "" ECU
VAL_ 100 Phantom 0 "x" ;
)";

    auto result = parser.parseString(dbc);
    EXPECT_EQ(result.totalSignalCount(), 1);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);
    EXPECT_EQ(signals->at(0).name, "Real");
    EXPECT_TRUE(signals->at(0).valueTable.empty());
}

TEST_F(DBCFileParserContractTest, SignalAfterBlankLinesParsedInOrder) {
    // Signals separated by multiple blank lines retain their declaration
    // order within the message.
    const std::string dbc = R"(BO_ 264 Msg: 8 DIR

 SG_ First : 0|8@1+ (1,0) [0|255] "" DIR


 SG_ Second : 8|8@1+ (1,0) [0|255] "" DIR
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(264);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 2);
    EXPECT_EQ(signals->at(0).name, "First");
    EXPECT_EQ(signals->at(1).name, "Second");
}

TEST_F(DBCFileParserContractTest, FullParse_DefinitionsUsableByMapper) {
    // End-to-end: a parsed definition, fed to the mapper, yields the expected
    // physical value. Locks that DBCFileParser output is consumable by
    // DBCSignalMapper without any transformation step.
    const std::string dbc = R"(BO_ 280 DI_state: 8 DI
 SG_ DI_gear : 21|3@1+ (1,0) [0|7] "" DI
VAL_ 280 DI_gear 1 "P" 4 "D" ;
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(280);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);

    // raw 4 (Drive) via the locked Intel 21|3 byte[2] encoding (0x95).
    std::vector<std::uint8_t> frame(8, 0);
    frame[2] = 0x95;
    auto v = DBCSignalMapper::mapSignal(frame, signals->at(0));
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 4.0);
}
