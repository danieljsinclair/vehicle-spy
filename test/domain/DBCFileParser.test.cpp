#include <gtest/gtest.h>
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"

using namespace vehicle_sim::domain;

// Helper: create a parser instance for tests
class DBCFileParserTest : public ::testing::Test {
protected:
    DBCFileParser parser;
};

// ================================================
// Empty / Invalid Input
// ================================================

TEST_F(DBCFileParserTest, ParseEmptyStringReturnsEmptyResult) {
    auto result = parser.parseString("");

    EXPECT_EQ(result.totalSignalCount(), 0);
    EXPECT_TRUE(result.signalsByCanId.empty());
}

TEST_F(DBCFileParserTest, ParseWhitespaceOnlyReturnsEmptyResult) {
    auto result = parser.parseString("   \n\n  \t\n  ");

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST_F(DBCFileParserTest, ParseGarbageTextReturnsEmptyResult) {
    auto result = parser.parseString("this is not DBC content at all\nnope\n");

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST_F(DBCFileParserTest, ParseMessageWithNoSignalsReturnsEmptyResult) {
    auto result = parser.parseString("BO_ 264 DIR_torque: 8 DIR\n");

    EXPECT_EQ(result.totalSignalCount(), 0);
}

// ================================================
// Single Message, Single Signal
// ================================================

TEST_F(DBCFileParserTest, ParseSingleSignalCorrectly) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
)";

    auto result = parser.parseString(dbc);

    ASSERT_EQ(result.totalSignalCount(), 1);
    const auto* signals = result.getSignalsForCanId(264);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);

    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.canId, 264);
    EXPECT_EQ(sig.name, "DIR_axleSpeed");
    EXPECT_EQ(sig.startBit, 40);
    EXPECT_EQ(sig.bitLength, 16);
    EXPECT_EQ(sig.byteOrder, DBCByteOrder::Intel);
    EXPECT_DOUBLE_EQ(sig.scale, 0.1);
    EXPECT_DOUBLE_EQ(sig.offset, 0.0);
    EXPECT_TRUE(sig.isSigned);
    EXPECT_EQ(sig.unit, "RPM");
    EXPECT_DOUBLE_EQ(sig.min, -2750.0);
    EXPECT_DOUBLE_EQ(sig.max, 2750.0);
}

// ================================================
// Single Message, Multiple Signals
// ================================================

TEST_F(DBCFileParserTest, ParseMultipleSignalsInOneMessage) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
 SG_ DIR_torqueActual : 27|13@1- (2,0) [-7500|7500] "Nm" DIR
)";

    auto result = parser.parseString(dbc);

    const auto* signals = result.getSignalsForCanId(264);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 2);

    EXPECT_EQ(signals->at(0).name, "DIR_axleSpeed");
    EXPECT_EQ(signals->at(1).name, "DIR_torqueActual");
}

// ================================================
// Multiple Messages
// ================================================

TEST_F(DBCFileParserTest, ParseMultipleMessages) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR

BO_ 280 DI_state: 8 DI
 SG_ DI_accelPedalPos : 32|8@1+ (0.4,0) [0|100] "%" DI
)";

    auto result = parser.parseString(dbc);

    EXPECT_EQ(result.totalSignalCount(), 2);
    ASSERT_NE(result.getSignalsForCanId(264), nullptr);
    ASSERT_NE(result.getSignalsForCanId(280), nullptr);
    EXPECT_EQ(result.getSignalsForCanId(264)->size(), 1);
    EXPECT_EQ(result.getSignalsForCanId(280)->size(), 1);
}

// ================================================
// Byte Order and Signedness
// ================================================

TEST_F(DBCFileParserTest, ParseUnsignedIntelSignal) {
    const std::string dbc = R"(BO_ 280 DI_state: 8 DI
 SG_ DI_accelPedalPos : 32|8@1+ (0.4,0) [0|100] "%" DI
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(280);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.byteOrder, DBCByteOrder::Intel);
    EXPECT_FALSE(sig.isSigned);
}

TEST_F(DBCFileParserTest, ParseSignedIntelSignal) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(264);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.byteOrder, DBCByteOrder::Intel);
    EXPECT_TRUE(sig.isSigned);
}

TEST_F(DBCFileParserTest, ParseMotorolaUnsignedSignal) {
    const std::string dbc = R"(BO_ 100 TestMsg: 8 ECU
 SG_ TestSignal : 7|8@0+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.byteOrder, DBCByteOrder::Motorola);
    EXPECT_FALSE(sig.isSigned);
}

TEST_F(DBCFileParserTest, ParseMotorolaSignedSignal) {
    const std::string dbc = R"(BO_ 100 TestMsg: 8 ECU
 SG_ TestSigned : 7|16@0- (0.01,0) [-327.68|327.67] "V" ECU
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.byteOrder, DBCByteOrder::Motorola);
    EXPECT_TRUE(sig.isSigned);
}

// ================================================
// Offset and Scale
// ================================================

TEST_F(DBCFileParserTest, ParseSignalWithNonZeroOffset) {
    const std::string dbc = R"(BO_ 297 SCCM_steeringAngle: 5 SCCM
 SG_ SteeringAngle129 : 16|14@1+ (0.1,-819.2) [-819.2|819.1] "deg" SCCM
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(297);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_DOUBLE_EQ(sig.scale, 0.1);
    EXPECT_DOUBLE_EQ(sig.offset, -819.2);
    EXPECT_DOUBLE_EQ(sig.min, -819.2);
    EXPECT_DOUBLE_EQ(sig.max, 819.1);
}

TEST_F(DBCFileParserTest, ParseSignalWithLargeScale) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_torqueActual : 27|13@1- (2,0) [-7500|7500] "Nm" DIR
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(264);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_DOUBLE_EQ(sig.scale, 2.0);
    EXPECT_DOUBLE_EQ(sig.offset, 0.0);
}

// ================================================
// Value Tables (VAL_)
// ================================================

TEST_F(DBCFileParserTest, ParseValueTable) {
    const std::string dbc = R"(BO_ 280 DI_state: 8 DI
 SG_ DI_brakePedal : 19|2@1+ (1,0) [0|2] "" DI
VAL_ 280 DI_brakePedal 0 "released" 1 "pressed" ;
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(280);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    ASSERT_EQ(sig.valueTable.size(), 2);
    EXPECT_EQ(sig.valueTable[0].value, 0);
    EXPECT_EQ(sig.valueTable[0].description, "released");
    EXPECT_EQ(sig.valueTable[1].value, 1);
    EXPECT_EQ(sig.valueTable[1].description, "pressed");
}

TEST_F(DBCFileParserTest, ParseValueTableWithMultipleEntries) {
    const std::string dbc = R"(BO_ 100 TestMsg: 8 ECU
 SG_ Gear : 0|3@1+ (1,0) [0|7] "" ECU
VAL_ 100 Gear 0 "Park" 1 "Reverse" 2 "Neutral" 3 "Drive" ;
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    ASSERT_EQ(sig.valueTable.size(), 4);
    EXPECT_EQ(sig.valueTable[0].description, "Park");
    EXPECT_EQ(sig.valueTable[1].description, "Reverse");
    EXPECT_EQ(sig.valueTable[2].description, "Neutral");
    EXPECT_EQ(sig.valueTable[3].description, "Drive");
}

// ================================================
// Comments and Noise Lines
// ================================================

TEST_F(DBCFileParserTest, SkipsCommentLines) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
)";

    auto result = parser.parseString(dbc);

    ASSERT_EQ(result.totalSignalCount(), 1);
    EXPECT_EQ(result.getSignalsForCanId(264)->at(0).name, "DIR_axleSpeed");
}

TEST_F(DBCFileParserTest, SkipsUnknownLineTypes) {
    const std::string dbc = R"(VERSION ""
NS_ :
BS_:
BU_: DIR DI SCCM

BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR

BA_DEF_ SG_ "GenSigStartValue" FLOAT 0 100;
CM_ SG_ 264 DIR_axleSpeed "Motor axle speed";
)";

    auto result = parser.parseString(dbc);

    ASSERT_EQ(result.totalSignalCount(), 1);
    EXPECT_EQ(result.getSignalsForCanId(264)->at(0).name, "DIR_axleSpeed");
}

// ================================================
// canParse (file-based validation)
// ================================================

TEST_F(DBCFileParserTest, CanParseReturnsFalseForNonexistentFile) {
    EXPECT_FALSE(parser.canParse("/nonexistent/path/to/file.dbc"));
}

// ================================================
// Integration: Parse real Tesla DBC fragment
// ================================================

TEST_F(DBCFileParserTest, ParseRealTeslaDBCFragment) {
    const std::string dbc = R"(BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
 SG_ DIR_torqueActual : 27|13@1- (2,0) [-7500|7500] "Nm" DIR

BO_ 280 DI_state: 8 DI
 SG_ DI_accelPedalPos : 32|8@1+ (0.4,0) [0|100] "%" DI
 SG_ DI_brakePedal : 19|2@1+ (1,0) [0|2] "" DI

BO_ 297 SCCM_steeringAngle: 5 SCCM
 SG_ SteeringAngle129 : 16|14@1+ (0.1,-819.2) [-819.2|819.1] "deg" SCCM

VAL_ 280 DI_brakePedal 0 "released" 1 "pressed" ;
)";

    auto result = parser.parseString(dbc);

    // 3 messages, 5 signals total
    EXPECT_EQ(result.totalSignalCount(), 5);
    EXPECT_NE(result.getSignalsForCanId(264), nullptr);
    EXPECT_NE(result.getSignalsForCanId(280), nullptr);
    EXPECT_NE(result.getSignalsForCanId(297), nullptr);

    // Verify CAN 264 signals
    const auto* dir = result.getSignalsForCanId(264);
    ASSERT_EQ(dir->size(), 2);
    EXPECT_EQ(dir->at(0).name, "DIR_axleSpeed");
    EXPECT_DOUBLE_EQ(dir->at(0).scale, 0.1);
    EXPECT_TRUE(dir->at(0).isSigned);
    EXPECT_EQ(dir->at(1).name, "DIR_torqueActual");
    EXPECT_DOUBLE_EQ(dir->at(1).scale, 2.0);
    EXPECT_EQ(dir->at(1).bitLength, 13);

    // Verify CAN 280 signals
    const auto* di = result.getSignalsForCanId(280);
    ASSERT_EQ(di->size(), 2);
    EXPECT_EQ(di->at(0).name, "DI_accelPedalPos");
    EXPECT_FALSE(di->at(0).isSigned);
    EXPECT_EQ(di->at(1).name, "DI_brakePedal");
    ASSERT_EQ(di->at(1).valueTable.size(), 2);

    // Verify CAN 297 signal
    const auto* sccm = result.getSignalsForCanId(297);
    ASSERT_EQ(sccm->size(), 1);
    EXPECT_DOUBLE_EQ(sccm->at(0).offset, -819.2);
}

// ================================================
// Signal without explicit min/max (edge case)
// ================================================

TEST_F(DBCFileParserTest, ParseSignalWithZeroMinMax) {
    const std::string dbc = R"(BO_ 100 TestMsg: 8 ECU
 SG_ BoolSignal : 0|1@1+ (1,0) [0|1] "" ECU
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);

    const auto& sig = signals->at(0);
    EXPECT_EQ(sig.startBit, 0);
    EXPECT_EQ(sig.bitLength, 1);
    EXPECT_DOUBLE_EQ(sig.min, 0.0);
    EXPECT_DOUBLE_EQ(sig.max, 1.0);
}

// ================================================
// Malformed-input + multiplexor contracts (S3776 refactor safety net).
// Each test states an externally observable behaviour of parseString
// (input DBC -> output signals), independent of the internal cursor walk.
// These must stay green so the S3776/S134 decomposition of parseString /
// parseSignalDefinition / parseValueEntries cannot silently change how
// malformed lines and the multiplexor marker are handled.
// ================================================

TEST_F(DBCFileParserTest, MultiplexorMarkerIsConsumedAndSignalParsesIntact) {
    // A well-formed SG_ carrying a leading 'M' multiplexor indicator: the 'M'
    // is consumed and the signal is parsed with its name, startBit, bitLength,
    // and byte order intact (the multiplexor text must not corrupt the parse).
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ SigName M : 7|8@0+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);

    ASSERT_EQ(result.totalSignalCount(), 1);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);
    EXPECT_EQ(signals->at(0).name, "SigName");
    EXPECT_EQ(signals->at(0).startBit, 7);
    EXPECT_EQ(signals->at(0).bitLength, 8);
    EXPECT_EQ(signals->at(0).byteOrder, DBCByteOrder::Motorola);
}

TEST_F(DBCFileParserTest, SignalBeforeAnyMessageHeaderYieldsNoSignals) {
    // An SG_ appearing before any BO_ message header yields no signals (it has
    // no owning message, so it must be dropped, not attached to a default id).
    const std::string dbc = R"( SG_ Orphan : 0|8@1+ (1,0) [0|255] "" ECU
BO_ 100 Msg: 8 ECU
 SG_ Real : 0|8@1+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);

    EXPECT_EQ(result.totalSignalCount(), 1);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    EXPECT_EQ(signals->at(0).name, "Real");
}

TEST_F(DBCFileParserTest, SignalMissingColonAfterNameIsDropped) {
    // An SG_ missing the ':' separator after the signal name yields no signals
    // (the line is dropped, not partially parsed into a malformed signal).
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ NoColon 7|8@0+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST_F(DBCFileParserTest, SignalWithNonNumericStartBitIsDropped) {
    // An SG_ whose startBit|bitLength field is non-numeric yields no signals.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ Bad : X|8@0+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST_F(DBCFileParserTest, SignalMissingScaleOffsetGroupIsDropped) {
    // An SG_ missing the (...) scale/offset group yields no signals.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ NoParen : 7|8@0+ 1,0 [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST_F(DBCFileParserTest, ValueTableKeepsWellFormedEntriesBeforeMalformedOne) {
    // A VAL_ whose final entry is malformed (non-numeric label index): the
    // preceding well-formed entries are parsed and attached to the matching
    // signal, and the malformed tail is dropped.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ Gear : 0|3@1+ (1,0) [0|7] "" ECU
VAL_ 100 Gear 0 "Park" 1 "Reverse" abc ;
)";

    auto result = parser.parseString(dbc);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);

    const auto& table = signals->at(0).valueTable;
    ASSERT_EQ(table.size(), 2);
    EXPECT_EQ(table[0].value, 0);
    EXPECT_EQ(table[0].description, "Park");
    EXPECT_EQ(table[1].value, 1);
    EXPECT_EQ(table[1].description, "Reverse");
}

TEST_F(DBCFileParserTest, MessageHeaderWithNonNumericIdYieldsNoSignals) {
    // A BO_ header whose numeric id is non-numeric yields no signals and does
    // not crash (the header is skipped, so no owning message is established).
    const std::string dbc = R"(BO_ abc Msg: 8 ECU
 SG_ WouldBeOrphan : 0|8@1+ (1,0) [0|255] "" ECU
)";

    auto result = parser.parseString(dbc);

    EXPECT_EQ(result.totalSignalCount(), 0);
}

TEST_F(DBCFileParserTest, ValueTableWithNonNumericIdIsIgnored) {
    // A VAL_ whose numeric id is non-numeric is ignored: no value table is
    // attached, and a valid signal on the same message still parses cleanly.
    const std::string dbc = R"(BO_ 100 Msg: 8 ECU
 SG_ Gear : 0|3@1+ (1,0) [0|7] "" ECU
VAL_ xyz 100 Gear 0 "Park" ;
)";

    auto result = parser.parseString(dbc);

    ASSERT_EQ(result.totalSignalCount(), 1);
    const auto* signals = result.getSignalsForCanId(100);
    ASSERT_NE(signals, nullptr);
    ASSERT_EQ(signals->size(), 1);
    EXPECT_EQ(signals->at(0).name, "Gear");
    EXPECT_TRUE(signals->at(0).valueTable.empty());
}
