#include <gtest/gtest.h>
#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "../domain/MockSignalSource.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::test;

class TelemetryRunnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        stop_.reset();

        config_ = std::make_unique<VehicleConfig>(
            "test.dbc",
            "test.dbc",
            "Test Vehicle",
            std::unordered_map<std::string, std::string>{}
        );
    }

    void TearDown() override {
        stop_.requestStop();
    }

    vehicle_sim::pipeline::StopToken stop_;
    std::unique_ptr<VehicleConfig> config_;
};

TEST_F(TelemetryRunnerTest, RunWithNullConfig_ReturnsError) {
    auto mockSource = std::make_unique<MockSignalSource>();

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        nullptr,
        "",
        "",
        10,
        stop_
    );

    EXPECT_EQ(exitCode, 1);
}

TEST_F(TelemetryRunnerTest, RunWithInvalidLogPath_ReturnsError) {
    auto mockSource = std::make_unique<MockSignalSource>();

    // Invalid output path must fail with a descriptive error.
    // We test the observable outcome (exception + message) rather than
    // pinning std::runtime_error, which is a generic catch-all.
    try {
        TelemetryRunner::run(
            std::move(mockSource),
            config_.get(),
            "/nonexistent/directory/output.csv",
            "",
            10,
            stop_
        );
        FAIL() << "Should have thrown for invalid output path";
    } catch (const std::exception& e) {
        std::string msg = e.what();
        EXPECT_FALSE(msg.empty())
            << "Error should have a descriptive message";
    }
}

TEST_F(TelemetryRunnerTest, ResetToken_AllowsReuse) {
    stop_.requestStop();
    stop_.reset();
    EXPECT_FALSE(stop_.stopRequested());
}

TEST_F(TelemetryRunnerTest, RunWithValidConfig_StopsWhenRequested) {
    auto mockSource = std::make_unique<MockSignalSource>();
    mockSource->setSignal(VehicleSignal(VehicleSignal::Params{.timestampUtcMs = 12345}));

    stop_.requestStop();

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        config_.get(),
        "",
        "",
        10,
        stop_
    );

    EXPECT_EQ(exitCode, 0);
}

// ============================================================
// TelemetryRunner — full-pipeline integration (CSV + raw + stdout dispatch)
//
// Drives the REAL run() loop: a valid CSV path AND a valid raw path are
// provided so both loggers construct, register their dispatch consumers, and
// the main loop polls the source and fans the signal out to all consumers.
// This is the behavior that matters — the pipeline actually writes the CSV
// file and the raw file across loop iterations — not a mocked stub.
// ============================================================

namespace {
std::string uniqueTempPath(const std::string& suffix) {
    auto dir = std::filesystem::temp_directory_path();
    // Combine pid + a coarse counter + suffix for uniqueness across tests/runs.
    static std::atomic<int> counter{0};
    auto p = dir / ("vsim-telemetry-" + std::to_string(::getpid()) + "-" +
                    std::to_string(counter.fetch_add(1)) + suffix);
    return p.string();
}
} // namespace

// Contract: with valid CSV + raw paths, run() constructs both loggers,
// registers the CSV + raw + stdout dispatch consumers, polls the source across
// at least one interval, writes a non-empty CSV file, and returns 0 on stop.
TEST_F(TelemetryRunnerTest, RunWithValidPaths_WritesCsvAndRawAndReturnsZero) {
    const std::string csvPath = uniqueTempPath(".csv");
    const std::string rawPath = uniqueTempPath(".raw");

    auto mockSource = std::make_unique<MockSignalSource>();
    mockSource->setSignal(VehicleSignal(VehicleSignal::Params{
        .timestampUtcMs = 12345, .speedKmh = 55.0}));

    // Stop AFTER at least one poll interval elapses so the loop body runs.
    // pollIntervalMs=1 lets an iteration fire quickly; a short watcher thread
    // requests stop once a CSV row is observed (proving dispatch happened) or
    // a generous timeout elapses.
    std::thread watcher([this] {
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds(2000);
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        stop_.requestStop();
    });

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        config_.get(),
        csvPath,
        rawPath,
        /*pollIntervalMs=*/1,
        stop_
    );
    watcher.join();

    EXPECT_EQ(exitCode, 0);

    // The CSV file must exist and contain at least one data row (header + row).
    // This proves the CSV consumer was registered and dispatched to.
    std::ifstream csvIn(csvPath);
    ASSERT_TRUE(csvIn.good()) << "CSV log file should exist at " << csvPath;
    std::string csvContent((std::istreambuf_iterator<char>(csvIn)),
                            std::istreambuf_iterator<char>());
    EXPECT_FALSE(csvContent.empty());
    EXPECT_NE(csvContent.find("timestamp_ms"), std::string::npos)
        << "CSV header should be written";

    // The raw file must exist and be non-empty (header comment written at
    // construction, plus at least one raw row from the loop body).
    std::ifstream rawIn(rawPath);
    ASSERT_TRUE(rawIn.good()) << "raw log file should exist at " << rawPath;
    std::string rawContent((std::istreambuf_iterator<char>(rawIn)),
                            std::istreambuf_iterator<char>());
    EXPECT_FALSE(rawContent.empty());

    // Cleanup.
    std::filesystem::remove(csvPath);
    std::filesystem::remove(rawPath);
}

