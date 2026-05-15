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
    EXPECT_EQ(content, "timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,steering_angle_deg,motor_rpm,motor_hv_voltage,motor_hv_current,gear_selector,motor_torque_nm");
}

TEST_F(TraceLoggerTest, WritesCompleteRowForAllFields) {
    TraceLogger logger(testFile);
    VehicleSignal signal(123456789ULL, 50.0, 100.0, 0.5, 25.0, -12.5, 3500.5, 400.0, 25.3, 150.0, "D");
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2); // header + 1 row
    EXPECT_EQ(lines[1], "123456789,50.00,100.00,0.50,25.00,-12.50,3500.50,400.00,25.30,D,150.00");
}

TEST_F(TraceLoggerTest, WritesMultipleRows) {
    TraceLogger logger(testFile);
    VehicleSignal signal1(1000ULL, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "P");
    VehicleSignal signal2(2000ULL, 100.0, 200.0, 2.0, 80.0, {}, 5000.0, 380.5, 25.2, 300.0, "N");

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
    EXPECT_EQ(lines[1], "1000,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,P,0.00"); // explicit zeros formatted
    EXPECT_EQ(lines[2], "2000,100.00,200.00,2.00,80.00,,5000.00,380.50,25.20,N,300.00");
}

TEST_F(TraceLoggerTest, LeavesEmptyCellsForNulloptValues) {
    TraceLogger logger(testFile);
    VehicleSignal signal(12345ULL);  // all fields nullopt
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "12345,,,,,,,,,,"); // timestamp + 10 empty cells
}

TEST_F(TraceLoggerTest, LeavesEmptyCellForNulloptGearSelector) {
    TraceLogger logger(testFile);
    VehicleSignal signal(12345ULL, 50.0, 100.0, {}, {}, {}, 3500.0, {}, {}, {});
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    // Order: timestamp, throttle, speed, accel, brake, steering, motor_rpm, motor_hv_voltage, motor_hv_current, gear_selector, motor_torque
    // Values: 12345, 50.00, 100.00, nullopt, nullopt, nullopt, 3500.00, nullopt, nullopt, nullopt, nullopt
    EXPECT_EQ(lines[1], "12345,50.00,100.00,,,,3500.00,,,,");
}

TEST_F(TraceLoggerTest, FormatsNegativeValuesCorrectly) {
    TraceLogger logger(testFile);
    VehicleSignal signal(12345ULL, {}, 50.0, -2.5, {}, -180.0, {}, {}, {}, -500.0, "R");
    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "12345,,50.00,-2.50,,-180.00,,,,R,-500.00"); // nullopt values are empty, negative values preserved
}

TEST_F(TraceLoggerTest, SupportsMoveSemantics) {
    TraceLogger logger1(testFile);
    VehicleSignal signal(12345ULL, 50.0, 100.0, 0.5, 25.0, {}, 3500.0, {}, {}, 150.0, "D");

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
    EXPECT_EQ(lines[1], "12345,50.00,100.00,0.50,25.00,,3500.00,,,D,150.00");
}

TEST_F(TraceLoggerTest, WorksAsEventDispatcherCallback) {
    TraceLogger logger(testFile);
    VehicleSignal signal(54321ULL, 75.0, 150.0, 1.0, 50.0, {}, 4000.0, {}, {}, 200.0, "S");

    logger(signal);

    std::string content = readFileContent(testFile);
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[1], "54321,75.00,150.00,1.00,50.00,,4000.00,,,S,200.00");
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
