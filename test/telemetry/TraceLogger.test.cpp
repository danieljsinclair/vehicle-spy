#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/RawTraceLogger.h"
#include "vehicle-sim/domain/VehicleSignal.h"

using namespace vehicle_sim::telemetry;
using namespace vehicle_sim::domain;

class TraceLoggerTest : public ::testing::Test {
protected:
    std::string testFile;
    std::string rawTestFile;

    void SetUp() override {
        testFile = "test_trace_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".csv";
        rawTestFile = "test_raw_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".log";
    }

    void TearDown() override {
        std::filesystem::remove(testFile);
        std::filesystem::remove(rawTestFile);
    }

    std::string readFileContent(const std::string& path) {
        std::ifstream file(path);
        if (!file) return "";
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string readFirstLine(const std::string& path) {
        std::ifstream file(path);
        if (!file) return "";
        std::string line;
        std::getline(file, line);
        return line;
    }
};

// ================================================
// TraceLogger Tests (CSV)
// ================================================

TEST_F(TraceLoggerTest, WritesHeaderOnConstruction) {
    TraceLogger logger(testFile);
    std::string content = readFirstLine(testFile);
    EXPECT_EQ(content, "timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,motor_rpm,gear_selector,motor_torque_nm");
}

TEST_F(TraceLoggerTest, WritesCompleteRowForAllFields) {
    TraceLogger logger(testFile);
    VehicleSignal signal(50.0, 100.0, 0.5, 25.0, 123456789ULL, 0.0, 3500.5, 0.0, 0.0, 150.0, "D");
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2); // header + 1 row
    EXPECT_EQ(lines[1], "123456789,50.00,100.00,0.50,25.00,3500.50,D,150.00");
}

TEST_F(TraceLoggerTest, WritesMultipleRows) {
    TraceLogger logger(testFile);
    VehicleSignal signal1(0.0, 0.0, 0.0, 0.0, 1000ULL, 0.0, 0.0, 0.0, 0.0, 0.0, "P");
    VehicleSignal signal2(100.0, 200.0, 2.0, 80.0, 2000ULL, 0.0, 5000.0, 0.0, 0.0, 300.0, "N");

    logger(signal1);
    logger(signal2);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 3); // header + 2 rows
    EXPECT_EQ(lines[1], "1000,,,,,,P,"); // all numeric zero values become empty
    EXPECT_EQ(lines[2], "2000,100.00,200.00,2.00,80.00,5000.00,N,300.00");
}

TEST_F(TraceLoggerTest, LeavesEmptyCellsForZeroValues) {
    TraceLogger logger(testFile);
    VehicleSignal signal(0.0, 0.0, 0.0, 0.0, 12345ULL, 0.0, 0.0, 0.0, 0.0, 0.0, "");
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "12345,,,,,,,"); // timestamp + 7 empty cells (6 numeric + 1 empty gear)
}

TEST_F(TraceLoggerTest, LeavesEmptyCellForEmptyGearSelector) {
    TraceLogger logger(testFile);
    VehicleSignal signal(50.0, 100.0, 0.0, 0.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 0.0, "");
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "12345,50.00,100.00,,,3500.00,,"); // zero values are empty
}

TEST_F(TraceLoggerTest, FormatsNegativeValuesCorrectly) {
    TraceLogger logger(testFile);
    VehicleSignal signal(0.0, 50.0, -2.5, 0.0, 12345ULL, 0.0, 0.0, 0.0, 0.0, -500.0, "R");
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "12345,,50.00,-2.50,,,R,-500.00"); // zero values are empty
}

TEST_F(TraceLoggerTest, SupportsMoveSemantics) {
    TraceLogger logger1(testFile);
    VehicleSignal signal(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 150.0, "D");

    TraceLogger logger2(std::move(logger1));
    logger2(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "12345,50.00,100.00,0.50,25.00,3500.00,D,150.00");
}

TEST_F(TraceLoggerTest, WorksAsEventDispatcherCallback) {
    TraceLogger logger(testFile);
    VehicleSignal signal(75.0, 150.0, 1.0, 50.0, 54321ULL, 0.0, 4000.0, 0.0, 0.0, 200.0, "S");

    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "54321,75.00,150.00,1.00,50.00,4000.00,S,200.00");
}

// ================================================
// RawTraceLogger Tests (Hex Dump)
// ================================================

TEST_F(TraceLoggerTest, RawLoggerWritesHeaderOnConstruction) {
    RawTraceLogger rawLogger(rawTestFile);
    std::string content = readFileContent(rawTestFile);

    EXPECT_TRUE(content.find("# vehicle-sim raw CAN capture") != std::string::npos);
    EXPECT_TRUE(content.find("# format: timestamp_utc_ms,data_hex") != std::string::npos);
}

TEST_F(TraceLoggerTest, RawLoggerWritesHexDumpForSingleByte) {
    RawTraceLogger rawLogger(rawTestFile);
    std::vector<std::uint8_t> data = {0xAB};
    rawLogger.write(123456789ULL, data);

    std::string content = readFileContent(rawTestFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 3); // 2 header + 1 data line
    EXPECT_EQ(lines[2], "123456789,ab");
}

TEST_F(TraceLoggerTest, RawLoggerWritesHexDumpForMultipleBytes) {
    RawTraceLogger rawLogger(rawTestFile);
    std::vector<std::uint8_t> data = {0x3C, 0x00, 0x18, 0x00, 0x04, 0xA0, 0x01, 0xFF};
    rawLogger.write(1746849600123ULL, data);

    std::string content = readFileContent(rawTestFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[2], "1746849600123,3c00180004a001ff");
}

TEST_F(TraceLoggerTest, RawLoggerWritesMultipleDataLines) {
    RawTraceLogger rawLogger(rawTestFile);
    rawLogger.write(1000ULL, {0x01, 0x02});
    rawLogger.write(2000ULL, {0xFF, 0xFE});

    std::string content = readFileContent(rawTestFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 4); // 2 header + 2 data lines
    EXPECT_EQ(lines[2], "1000,0102");
    EXPECT_EQ(lines[3], "2000,fffe");
}

TEST_F(TraceLoggerTest, RawLoggerHandlesEmptyData) {
    RawTraceLogger rawLogger(rawTestFile);
    std::vector<std::uint8_t> data = {};
    rawLogger.write(12345ULL, data);

    std::string content = readFileContent(rawTestFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[2], "12345,");
}

TEST_F(TraceLoggerTest, RawLoggerHandlesAllZeroBytes) {
    std::string zeroTestFile = "test_raw_zero_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".log";
    {
        RawTraceLogger rawLogger(zeroTestFile);
        std::vector<std::uint8_t> data(8, 0x00);
        rawLogger.write(12345ULL, data);

        std::string content = readFileContent(zeroTestFile);
        std::vector<std::string> lines;
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }

        ASSERT_EQ(lines.size(), 3);
        EXPECT_EQ(lines[2], "12345,0000000000000000");
    }
    std::filesystem::remove(zeroTestFile);
}

TEST_F(TraceLoggerTest, RawLoggerSupportsMoveSemantics) {
    RawTraceLogger rawLogger1(rawTestFile);
    RawTraceLogger rawLogger2(std::move(rawLogger1));

    rawLogger2.write(99999ULL, {0xDE, 0xAD, 0xBE, 0xEF});

    std::string content = readFileContent(rawTestFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[2], "99999,deadbeef");
}

TEST_F(TraceLoggerTest, RawLoggerAppendsToFile) {
    {
        RawTraceLogger rawLogger1(rawTestFile);
        rawLogger1.write(1000ULL, {0x01, 0x02});
    }

    RawTraceLogger rawLogger2(rawTestFile);
    rawLogger2.write(2000ULL, {0x03, 0x04});

    std::string content = readFileContent(rawTestFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 6); // 2 header + 2 data from first + 2 header + 1 data from second
    EXPECT_TRUE(content.find("1000,0102") != std::string::npos);
    EXPECT_TRUE(content.find("2000,0304") != std::string::npos);
}

// ================================================
// VehicleSignal New Field Tests
// (note: comprehensive EV field tests in VehicleSignalEV.test.cpp)
// ================================================

TEST(VehicleSignalNewFieldsTest, MotorRpmIsClampedToMax20000) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 25000.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 20000.0);
}

TEST(VehicleSignalNewFieldsTest, MotorRpmIsClampedToMin0) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, -500.0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 0.0);
}

TEST(VehicleSignalNewFieldsTest, MotorRpmAcceptsValidRange) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 15000.5);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 15000.5);
}

TEST(VehicleSignalNewFieldsTest, GearSelectorStoresValidValues) {
    VehicleSignal signalP(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, "P");
    VehicleSignal signalR(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, "R");
    VehicleSignal signalN(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, "N");
    VehicleSignal signalD(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, "D");
    VehicleSignal signalS(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, "S");

    EXPECT_EQ(signalP.getGearSelector(), "P");
    EXPECT_EQ(signalR.getGearSelector(), "R");
    EXPECT_EQ(signalN.getGearSelector(), "N");
    EXPECT_EQ(signalD.getGearSelector(), "D");
    EXPECT_EQ(signalS.getGearSelector(), "S");
}

TEST(VehicleSignalNewFieldsTest, GearSelectorStoresEmptyStringByDefault) {
    VehicleSignal signal(0, 0, 0, 0, 0);
    EXPECT_TRUE(signal.getGearSelector().empty());
}

TEST(VehicleSignalNewFieldsTest, GearSelectorMovesInputCorrectly) {
    std::string gear = "D";
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, std::move(gear));
    EXPECT_EQ(signal.getGearSelector(), "D");
}

TEST(VehicleSignalNewFieldsTest, MotorTorqueNmIsClampedToMax7500) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 8000.0, "");
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 7500.0);
}

TEST(VehicleSignalNewFieldsTest, MotorTorqueNmIsClampedToMinNegative7500) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, -8000.0, "");
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), -7500.0);
}

TEST(VehicleSignalNewFieldsTest, MotorTorqueNmAcceptsValidRange) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 375.5, "");
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 375.5);
}

TEST(VehicleSignalNewFieldsTest, MotorTorqueNmAcceptsNegativeValues) {
    VehicleSignal signal(0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, -250.5, "");
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), -250.5);
}

TEST(VehicleSignalNewFieldsTest, MotorTorqueNmIsZeroByDefault) {
    VehicleSignal signal(0, 0, 0, 0, 0);
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 0.0);
}

TEST(VehicleSignalNewFieldsTest, MotorRpmIsZeroByDefault) {
    VehicleSignal signal(0, 0, 0, 0, 0);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 0.0);
}

TEST(VehicleSignalNewFieldsTest, EqualityIncludesNewFields) {
    VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 150.0, "D");
    VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 150.0, "D");
    VehicleSignal c(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 4000.0, 0.0, 0.0, 150.0, "D"); // different rpm
    VehicleSignal d(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 150.0, "N"); // different gear
    VehicleSignal e(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 200.0, "D"); // different torque

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(a, e);
}

TEST(VehicleSignalNewFieldsTest, InequalityIncludesNewFields) {
    VehicleSignal a(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 150.0, "D");
    VehicleSignal b(50.0, 100.0, 0.5, 25.0, 12345ULL, 0.0, 3500.0, 0.0, 0.0, 150.0, "N");

    EXPECT_NE(a, b);
}

TEST(VehicleSignalNewFieldsTest, AllFieldsConstructCorrectly) {
    VehicleSignal signal(50.0, 100.0, 0.5, 25.0, 123456789ULL, 0.0, 5000.0, 0.0, 0.0, 300.0, "D");

    EXPECT_DOUBLE_EQ(signal.getThrottlePercent(), 50.0);
    EXPECT_DOUBLE_EQ(signal.getSpeedKmh(), 100.0);
    EXPECT_DOUBLE_EQ(signal.getAccelerationG(), 0.5);
    EXPECT_DOUBLE_EQ(signal.getBrakePercent(), 25.0);
    EXPECT_EQ(signal.getTimestampUtcMs(), 123456789ULL);
    EXPECT_DOUBLE_EQ(signal.getMotorRpm(), 5000.0);
    EXPECT_EQ(signal.getGearSelector(), "D");
    EXPECT_DOUBLE_EQ(signal.getMotorTorqueNm(), 300.0);
}
