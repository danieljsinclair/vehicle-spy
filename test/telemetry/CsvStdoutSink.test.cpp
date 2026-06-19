#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include "vehicle-sim/telemetry/CsvStdoutSink.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/EventDispatcher.h"

using namespace vehicle_sim::telemetry;
using namespace vehicle_sim::domain;

class CsvStdoutSinkTest : public ::testing::Test {
protected:
    std::ostringstream out;
};

// ================================================
// CsvStdoutSink Tests
// ================================================

TEST_F(CsvStdoutSinkTest, WritesHeaderOnConstruction) {
    CsvStdoutSink sink(out);
    std::string content = out.str();
    // Header must be the first line, identical to TraceLogger column order
    EXPECT_TRUE(content.find("timestamp_utc_ms,throttle_pct,speed_kmh,acceleration_g,brake_pct,steering_angle_deg,motor_rpm,motor_hv_voltage,motor_hv_current,gear_selector,motor_torque_nm") != std::string::npos);
}

TEST_F(CsvStdoutSinkTest, WritesRowForAllFields) {
    CsvStdoutSink sink(out);
    VehicleSignal signal(123456789ULL, 50.0, 100.0, 0.5, 25.0, -12.5, 3500.5, 400.0, 25.3, 150.0, 4097);
    sink(signal);

    std::string content = out.str();
    // Must contain a row identical to what TraceLogger would write
    EXPECT_TRUE(content.find("123456789,50.00,100.00,0.50,25.00,-12.50,3500.50,400.00,25.30,4097,150.00") != std::string::npos);
}

TEST_F(CsvStdoutSinkTest, WritesMultipleRows) {
    CsvStdoutSink sink(out);
    VehicleSignal signal1(1000ULL, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -2);
    VehicleSignal signal2(2000ULL, 100.0, 200.0, 2.0, 80.0, {}, 5000.0, 380.5, 25.2, 300.0, 0);

    sink(signal1);
    sink(signal2);

    std::string content = out.str();
    EXPECT_TRUE(content.find("1000,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,-2,0.00") != std::string::npos);
    EXPECT_TRUE(content.find("2000,100.00,200.00,2.00,80.00,,5000.00,380.50,25.20,0,300.00") != std::string::npos);
}

TEST_F(CsvStdoutSinkTest, LeavesEmptyCellsForNulloptValues) {
    CsvStdoutSink sink(out);
    VehicleSignal signal(12345ULL);
    sink(signal);

    std::string content = out.str();
    EXPECT_TRUE(content.find("12345,,,,,,,,,,") != std::string::npos);
}

TEST_F(CsvStdoutSinkTest, OutputIsIdenticalFormatToTraceLogger) {
    // The whole point: CsvStdoutSink must produce rows identical to TraceLogger
    CsvStdoutSink sink(out);
    // Constructor params: timestamp, throttle, speed, accel, brake, steering, motorRpm, hvVoltage, hvCurrent, motorTorque, gearSelector
    VehicleSignal signal(99999ULL, 42.5, 88.0, 1.2, 0.0, 5.0, 2000.0, 350.0, 12.5, 75.0, 4096);
    sink(signal);

    std::string content = out.str();
    // Extract the data line (skip header line ending at first '\n')
    auto newlinePos = content.find('\n');
    ASSERT_NE(newlinePos, std::string::npos);
    std::string dataLine = content.substr(newlinePos + 1);
    // Strip trailing newline that writeRow appends
    if (!dataLine.empty() && dataLine.back() == '\n') {
        dataLine.pop_back();
    }
    // TraceLogger column order: timestamp,throttle,speed,accel,brake,steering,motor_rpm,hv_voltage,hv_current,gear_selector,motor_torque
    // Note: gear_selector comes BEFORE motor_torque in the CSV output
    EXPECT_EQ(dataLine, "99999,42.50,88.00,1.20,0.00,5.00,2000.00,350.00,12.50,4096,75.00");
}

TEST_F(CsvStdoutSinkTest, WorksAsEventDispatcherCallback) {
    CsvStdoutSink sink(out);
    // The sink must be callable as a SignalCallback (std::function<void(const VehicleSignal&)>).
    // CsvStdoutSink is move-only, so wrap in a lambda that calls through reference.
    auto callback = [&sink](const vehicle_sim::domain::VehicleSignal& signal) { sink(signal); };
    VehicleSignal signal(54321ULL, 75.0, 150.0, 1.0, 50.0, {}, 4000.0, {}, {}, 200.0, 4098);
    callback(signal);

    std::string content = out.str();
    EXPECT_TRUE(content.find("54321,75.00,150.00,1.00,50.00,,4000.00,,,4098,200.00") != std::string::npos);
}

// ================================================
// NullCsvStdoutSink Tests (no-op when disabled)
// ================================================

TEST_F(CsvStdoutSinkTest, NullSinkDoesNotWrite) {
    NullCsvStdoutSink nullSink;
    VehicleSignal signal(12345ULL, 50.0, 100.0, {}, {}, {}, {}, {}, {}, {}, {});
    // Must not throw, must not write to any stream
    nullSink(signal);
    SUCCEED();
}

// ================================================
// Factory Tests
// ================================================

TEST_F(CsvStdoutSinkTest, CreateStdoutSinkReturnsSinkWhenEnabled) {
    auto sink = createStdoutSink(true, out);
    ASSERT_NE(sink, nullptr);
    // Should write header on construction
    EXPECT_TRUE(out.str().find("timestamp_utc_ms") != std::string::npos);
}

TEST_F(CsvStdoutSinkTest, CreateStdoutSinkReturnsNullSinkWhenDisabled) {
    std::ostringstream dummy;
    auto sink = createStdoutSink(false, dummy);
    ASSERT_NE(sink, nullptr);
    // Should NOT write anything
    EXPECT_TRUE(dummy.str().empty());
}


// ================================================
// TelemetryRunner integration tests
// ================================================

#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "../domain/MockSignalSource.h"
#include <thread>
#include <chrono>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::test;

class CsvStdoutIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        TelemetryRunner::resetRunningState();
        config_ = std::make_unique<VehicleConfig>(
            "test.dbc", "test.dbc", "Test Vehicle",
            std::unordered_map<std::string, std::string>{}
        );
    }

    void TearDown() override {
        TelemetryRunner::requestStop();
    }

    std::unique_ptr<VehicleConfig> config_;
};

TEST_F(CsvStdoutIntegrationTest, RunWithStdoutCsv_WritesCsvRowsToProvidedStream) {
    auto mockSource = std::make_unique<MockSignalSource>();
    mockSource->setSignal(VehicleSignal(11111ULL, 25.0, 50.0, {}, {}, {}, 1500.0, {}, {}, {}, {}));

    std::ostringstream csvCapture;
    std::thread stopper([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        TelemetryRunner::requestStop();
    });

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        config_.get(),
        "",     // no log-csv
        "",     // no log-raw
        10,     // poll interval
        true,   // stdout-csv
        &csvCapture
    );

    stopper.join();
    EXPECT_EQ(exitCode, 0);

    std::string output = csvCapture.str();
    // Must contain the CSV header
    EXPECT_TRUE(output.find("timestamp_utc_ms") != std::string::npos);
    // Must contain a data row - check for partial match to avoid comma-counting issues
    EXPECT_TRUE(output.find("11111,25.00,50.00") != std::string::npos);
    EXPECT_TRUE(output.find("1500.00") != std::string::npos);
    // Verify multiple rows were written (header + at least 2 data rows)
    auto firstRow = output.find("11111,25.00,50.00");
    ASSERT_NE(firstRow, std::string::npos);
    auto secondRow = output.find("11111,25.00,50.00", firstRow + 1);
    EXPECT_NE(secondRow, std::string::npos);
}

TEST_F(CsvStdoutIntegrationTest, RunWithoutStdoutCsv_DoesNotWriteCsvRows) {
    auto mockSource = std::make_unique<MockSignalSource>();
    mockSource->setSignal(VehicleSignal(22222ULL, 10.0, 20.0, {}, {}, {}, {}, {}, {}, {}, {}));

    std::ostringstream csvCapture;
    std::thread stopper([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        TelemetryRunner::requestStop();
    });

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        config_.get(),
        "",     // no log-csv
        "",     // no log-raw
        10,     // poll interval
        false,  // no stdout-csv
        &csvCapture
    );

    stopper.join();
    EXPECT_EQ(exitCode, 0);

    // When stdout-csv is disabled, no CSV rows should appear
    EXPECT_TRUE(csvCapture.str().empty());
}
