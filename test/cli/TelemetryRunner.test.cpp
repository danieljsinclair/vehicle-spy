#include <gtest/gtest.h>
#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "../domain/MockSignalSource.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"

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
