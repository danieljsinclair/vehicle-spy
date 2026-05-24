#include <gtest/gtest.h>
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;

class CliOptionsTest : public ::testing::Test {
protected:
    struct Args {
        std::vector<std::string> strings;
        std::vector<char*> ptrs;

        explicit Args(std::vector<std::string> args) : strings(std::move(args)) {
            ptrs.reserve(strings.size());
            for (auto& s : strings) {
                ptrs.push_back(s.data());
            }
        }

        int argc() const { return static_cast<int>(ptrs.size()); }
        char** argv() { return ptrs.data(); }
    };
};

TEST_F(CliOptionsTest, DefaultsWhenNoArgs) {
    Args args({"vehicle-sim"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.scan_mode);
    EXPECT_FALSE(opts.help_requested);
    EXPECT_FALSE(opts.list_signals);
    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.update_interval_ms, DEFAULT_UPDATE_INTERVAL_MS);
    EXPECT_EQ(opts.format, DEFAULT_FORMAT);
    EXPECT_TRUE(opts.connect_target.empty());
    EXPECT_TRUE(opts.vehicle_type.empty());
}

TEST_F(CliOptionsTest, HelpFlag) {
    Args args({"vehicle-sim", "--help"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.help_requested);
}

TEST_F(CliOptionsTest, HelpShortFlag) {
    Args args({"vehicle-sim", "-h"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.help_requested);
}

TEST_F(CliOptionsTest, ScanFlag) {
    Args args({"vehicle-sim", "--scan"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.scan_mode);
}

TEST_F(CliOptionsTest, ScanShortFlag) {
    Args args({"vehicle-sim", "-s"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.scan_mode);
}

TEST_F(CliOptionsTest, ConnectDemo) {
    Args args({"vehicle-sim", "--connect", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.connect_target, "demo");
    EXPECT_TRUE(opts.isDemo());
    EXPECT_FALSE(opts.isBLE());
}

TEST_F(CliOptionsTest, ConnectDemoShortFlag) {
    Args args({"vehicle-sim", "-c", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.connect_target, "demo");
    EXPECT_TRUE(opts.isDemo());
}

TEST_F(CliOptionsTest, ConnectBLEAddress) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.connect_target, "AA:BB:CC:DD:EE:FF");
    EXPECT_FALSE(opts.isDemo());
    EXPECT_TRUE(opts.isBLE());
    EXPECT_EQ(opts.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, ConnectDemoWithVehicle) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.connect_target, "demo");
    EXPECT_EQ(opts.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, InvalidVehicleTypeParsesWithoutError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "invalid"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.vehicle_type, "invalid");
}

TEST_F(CliOptionsTest, ConnectWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, VehicleWithoutConnectParses) {
    Args args({"vehicle-sim", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.connect_target.empty());
}

TEST_F(CliOptionsTest, IntervalOverride) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--interval", "250"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.update_interval_ms, 250);
}

TEST_F(CliOptionsTest, IntervalShortFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "-i", "100"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.update_interval_ms, 100);
}

TEST_F(CliOptionsTest, IntervalWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--interval"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, FormatOverride) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--format", "json"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.format, "json");
}

TEST_F(CliOptionsTest, FormatWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--format"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ListSignalsFlag) {
    Args args({"vehicle-sim", "--list"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.list_signals);
}

TEST_F(CliOptionsTest, UnknownArgReturnsError) {
    Args args({"vehicle-sim", "--bogus"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ValidTeslaVehicleType) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, ValidAudiVehicleType) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "audi_mlb_evo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.vehicle_type, "audi_mlb_evo");
}

TEST_F(CliOptionsTest, ValidGenericVehicleType) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "generic"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.vehicle_type, "generic");
}

TEST_F(CliOptionsTest, LogCsvFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-csv", "trace.csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.log_csv, "trace.csv");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogRawFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-raw", "raw.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.log_raw, "raw.log");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, BothLogFlags) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-csv", "trace.csv", "--log-raw", "raw.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.log_csv, "trace.csv");
    EXPECT_EQ(opts.log_raw, "raw.log");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogCsvWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogRawWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-raw"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ScanWithoutConnectNoError) {
    Args args({"vehicle-sim", "--scan"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.scan_mode);
    EXPECT_TRUE(opts.connect_target.empty());
    EXPECT_TRUE(opts.vehicle_type.empty());
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ListWithoutConnectNoError) {
    Args args({"vehicle-sim", "--list"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.list_signals);
    EXPECT_TRUE(opts.connect_target.empty());
    EXPECT_TRUE(opts.vehicle_type.empty());
    EXPECT_TRUE(opts.error_message.empty());
}

// Validation tests (integration with registry)
class CliValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        DefaultVehicleConfigs::registerAll(service_.registry());
    }

    struct Args {
        std::vector<std::string> strings;
        std::vector<char*> ptrs;

        explicit Args(std::vector<std::string> args) : strings(std::move(args)) {
            ptrs.reserve(strings.size());
            for (auto& s : strings) {
                ptrs.push_back(s.data());
            }
        }

        int argc() const { return static_cast<int>(ptrs.size()); }
        char** argv() { return ptrs.data(); }
    };

    DBCTranslationService service_;
};

TEST_F(CliValidationTest, ValidateWithoutConnect_ReturnsError) {
    Args args({"vehicle-sim", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("--connect"), std::string::npos);
}

TEST_F(CliValidationTest, ValidateWithoutVehicle_ReturnsErrorWithAvailable) {
    Args args({"vehicle-sim", "--connect", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("--vehicle"), std::string::npos);
    EXPECT_NE(error.find("Available:"), std::string::npos);
    EXPECT_NE(error.find("tesla"), std::string::npos);
}

TEST_F(CliValidationTest, ValidateInvalidVehicle_ReturnsErrorWithAvailable) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "invalid"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("invalid"), std::string::npos);
    EXPECT_NE(error.find("Available:"), std::string::npos);
}

TEST_F(CliValidationTest, ValidateVehicleAuto_NoError) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "auto"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
}

TEST_F(CliValidationTest, ValidateVehicleAuto_DemoConnect_ReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "auto"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("auto requires a BLE connection"), std::string::npos);
}

TEST_F(CliValidationTest, ValidateValidVehicle_NoError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
}

TEST_F(CliValidationTest, ValidateScan_NoError) {
    Args args({"vehicle-sim", "--scan"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
}
