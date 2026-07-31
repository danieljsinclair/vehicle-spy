#include <gtest/gtest.h>
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include <sstream>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;

class OrchestrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<DBCTranslationService>();
        DefaultVehicleConfigs::registerAll(service_->registry());
    }

    std::unique_ptr<DBCTranslationService> service_;
};

TEST_F(OrchestrationTest, HandleEarlyExit_WithErrorMessage_ReturnsTrue) {
    CliOptions opts;
    opts.error_message = "Invalid argument: unknown flag";

    bool shouldExit = handleEarlyExit(opts, *service_);

    EXPECT_TRUE(shouldExit) << "Should exit when error message is present";
}

TEST_F(OrchestrationTest, HandleEarlyExit_WithHelpFlag_ReturnsTrue) {
    CliOptions opts;
    opts.help_requested = true;

    bool shouldExit = handleEarlyExit(opts, *service_);

    EXPECT_TRUE(shouldExit) << "Should exit when help is requested";
}

TEST_F(OrchestrationTest, HandleEarlyExit_WithListSignalsFlag_ReturnsTrue) {
    CliOptions opts;
    opts.list_signals = true;

    bool shouldExit = handleEarlyExit(opts, *service_);

    EXPECT_TRUE(shouldExit) << "Should exit when listing signals";
}

TEST_F(OrchestrationTest, HandleEarlyExit_WithNoEarlyExitConditions_ReturnsFalse) {
    CliOptions opts;
    opts.help_requested = false;
    opts.list_signals = false;
    opts.error_message.clear();

    bool shouldExit = handleEarlyExit(opts, *service_);

    EXPECT_FALSE(shouldExit) << "Should not exit when no early exit conditions are present";
}

TEST_F(OrchestrationTest, HandleEarlyExit_HelpFlagOutputsToStdout) {
    CliOptions opts;
    opts.help_requested = true;

    // Redirect stdout
    std::stringstream output;
    std::streambuf* old = std::cout.rdbuf(output.rdbuf());

    (void)handleEarlyExit(opts, *service_);

    std::cout.rdbuf(old);

    std::string helpText = output.str();
    EXPECT_FALSE(helpText.empty()) << "Help text should be output";
    EXPECT_TRUE(helpText.find("Usage") != std::string::npos ||
                helpText.find("vehicle-sim") != std::string::npos)
        << "Help text should contain usage information";
}

TEST_F(OrchestrationTest, HandleEarlyExit_ListSignalsOutputsToStdout) {
    CliOptions opts;
    opts.list_signals = true;

    std::stringstream output;
    std::streambuf* old = std::cout.rdbuf(output.rdbuf());

    (void)handleEarlyExit(opts, *service_);

    std::cout.rdbuf(old);

    std::string listText = output.str();
    EXPECT_FALSE(listText.empty()) << "List signals should output text";
}

TEST_F(OrchestrationTest, ResolveVehicleContext_ValidVehicleReturnsContext) {
    auto context = resolveVehicleContext("tesla", *service_);

    EXPECT_NE(context.config, nullptr) << "Config should not be null";
    EXPECT_EQ(context.vehicleType, "tesla");
    EXPECT_EQ(context.protocol, VehicleProtocol::CAN);
    EXPECT_EQ(context.config->vehicleName, "Tesla Model 3");
}

TEST_F(OrchestrationTest, ResolveVehicleContext_AudiVehicleReturnsCANProtocol) {
    auto context = resolveVehicleContext("audi_mlb_evo", *service_);

    EXPECT_NE(context.config, nullptr) << "Config should not be null";
    EXPECT_EQ(context.vehicleType, "audi_mlb_evo");
    EXPECT_EQ(context.protocol, VehicleProtocol::CAN);
    EXPECT_EQ(context.config->vehicleName, "Audi MLB Evo");
}

TEST_F(OrchestrationTest, ResolveVehicleContext_ErrorMessageIncludesAvailableVehicles) {
    try {
        (void)resolveVehicleContext("nonexistent_vehicle", *service_);
        FAIL() << "Should have thrown runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("tesla") != std::string::npos ||
                    msg.find("audi_mlb_evo") != std::string::npos)
            << "Error should mention available vehicles";
    }
}

TEST_F(OrchestrationTest, PrintBanner_DoesNotThrow) {
    EXPECT_NO_THROW(printBanner());
}