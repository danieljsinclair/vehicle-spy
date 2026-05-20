#include <gtest/gtest.h>
#include "vehicle-sim/cli/TelemetryRunner.h"
#include "../domain/MockSignalSource.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include <fstream>
#include <cstdio>
#include <thread>
#include <chrono>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::test;

class TelemetryRunnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        TelemetryRunner::resetRunningState();

        config_ = std::make_unique<VehicleConfig>(
            "test.dbc",
            "test.dbc",
            "Test Vehicle",
            std::unordered_map<std::string, std::string>{}
        );
    }

    void TearDown() override {
        TelemetryRunner::requestStop();
    }

    std::unique_ptr<VehicleConfig> config_;
};

TEST_F(TelemetryRunnerTest, RunWithNullConfig_ReturnsError) {
    auto mockSource = std::make_unique<MockSignalSource>();

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        nullptr,
        "",
        "",
        10
    );

    EXPECT_EQ(exitCode, 1);
}

TEST_F(TelemetryRunnerTest, RunWithInvalidLogPath_ReturnsError) {
    auto mockSource = std::make_unique<MockSignalSource>();

    EXPECT_THROW({
        TelemetryRunner::run(
            std::move(mockSource),
            config_.get(),
            "/nonexistent/directory/output.csv",
            "",
            10
        );
    }, std::runtime_error);
}

TEST_F(TelemetryRunnerTest, ResetRunningState_AllowsReuse) {
    TelemetryRunner::resetRunningState();
    SUCCEED();
}

TEST_F(TelemetryRunnerTest, RunWithValidConfig_StopsWhenRequested) {
    auto mockSource = std::make_unique<MockSignalSource>();
    mockSource->setSignal(VehicleSignal(12345));

    std::thread stopper([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        TelemetryRunner::requestStop();
    });

    int exitCode = TelemetryRunner::run(
        std::move(mockSource),
        config_.get(),
        "",
        "",
        10
    );

    stopper.join();
    EXPECT_EQ(exitCode, 0);
}
