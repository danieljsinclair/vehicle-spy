#include <gtest/gtest.h>
#include "vehicle-sim/cli/CliOptions.h"

using namespace vehicle_sim::cli;

class CliOptionsTest : public ::testing::Test {
protected:
    // Helper to build argc/argv from a list of strings
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
    EXPECT_FALSE(opts.connect_mode);
    EXPECT_FALSE(opts.simulate_mode);
    EXPECT_FALSE(opts.help_requested);
    EXPECT_FALSE(opts.list_signals);
    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.update_interval_ms, DEFAULT_UPDATE_INTERVAL_MS);
    EXPECT_EQ(opts.format, DEFAULT_FORMAT);
    EXPECT_TRUE(opts.vehicle_type.empty());
}

TEST_F(CliOptionsTest, SimulateFlag) {
    Args args({"vehicle-sim", "--simulate"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.simulate_mode);
    EXPECT_FALSE(opts.help_requested);
}

TEST_F(CliOptionsTest, SimulateShortFlag) {
    Args args({"vehicle-sim", "-m"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.simulate_mode);
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

TEST_F(CliOptionsTest, ConnectWithAddress) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_EQ(opts.connect_address, "AA:BB:CC:DD:EE:FF");
}

TEST_F(CliOptionsTest, ConnectShortFlag) {
    Args args({"vehicle-sim", "-c", "11:22:33:44:55:66"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_EQ(opts.connect_address, "11:22:33:44:55:66");
}

TEST_F(CliOptionsTest, ConnectWithoutAddressReturnsError) {
    Args args({"vehicle-sim", "--connect"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.connect_mode);
    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("connect"), std::string::npos);
}

TEST_F(CliOptionsTest, VehicleType) {
    Args args({"vehicle-sim", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, VehicleWithoutTypeReturnsError) {
    Args args({"vehicle-sim", "--vehicle"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("vehicle"), std::string::npos);
}

TEST_F(CliOptionsTest, IntervalOverride) {
    Args args({"vehicle-sim", "--interval", "250"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.update_interval_ms, 250);
}

TEST_F(CliOptionsTest, IntervalShortFlag) {
    Args args({"vehicle-sim", "-i", "100"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.update_interval_ms, 100);
}

TEST_F(CliOptionsTest, IntervalWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--interval"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("interval"), std::string::npos);
}

TEST_F(CliOptionsTest, FormatOverride) {
    Args args({"vehicle-sim", "--format", "json"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.format, "json");
}

TEST_F(CliOptionsTest, FormatWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--format"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("format"), std::string::npos);
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
    EXPECT_NE(opts.error_message.find("bogus"), std::string::npos);
}

TEST_F(CliOptionsTest, MultipleFlagsCombined) {
    Args args({"vehicle-sim", "--simulate", "--vehicle", "tesla", "--interval", "100"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.simulate_mode);
    EXPECT_EQ(opts.vehicle_type, "tesla");
    EXPECT_EQ(opts.update_interval_ms, 100);
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectWithoutVehicleReturnsError) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("Vehicle type required"), std::string::npos);
    EXPECT_NE(opts.error_message.find("--connect"), std::string::npos);
}

TEST_F(CliOptionsTest, ConnectWithInvalidVehicleReturnsError) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "foobar"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_NE(opts.error_message.find("foobar"), std::string::npos);
    EXPECT_NE(opts.error_message.find("tesla"), std::string::npos);
    EXPECT_NE(opts.error_message.find("audi_mlb_evo"), std::string::npos);
    EXPECT_NE(opts.error_message.find("generic"), std::string::npos);
}

TEST_F(CliOptionsTest, ConnectWithValidTeslaVehicleNoError) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_EQ(opts.vehicle_type, "tesla");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectWithValidAudiVehicleNoError) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "audi_mlb_evo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_EQ(opts.vehicle_type, "audi_mlb_evo");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectWithValidGenericVehicleNoError) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "generic"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_mode);
    EXPECT_EQ(opts.vehicle_type, "generic");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, SimulateWithoutVehicleNoErrorDefaultsToGeneric) {
    Args args({"vehicle-sim", "--simulate"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.simulate_mode);
    EXPECT_TRUE(opts.vehicle_type.empty());
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ScanWithoutVehicleNoError) {
    Args args({"vehicle-sim", "--scan"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.scan_mode);
    EXPECT_TRUE(opts.vehicle_type.empty());
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogCsvFlag) {
    Args args({"vehicle-sim", "--log-csv", "trace.csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.log_csv, "trace.csv");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogRawFlag) {
    Args args({"vehicle-sim", "--log-raw", "raw.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.log_raw, "raw.log");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, BothLogFlags) {
    Args args({"vehicle-sim", "--log-csv", "trace.csv", "--log-raw", "raw.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.log_csv, "trace.csv");
    EXPECT_EQ(opts.log_raw, "raw.log");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogCsvWithSimulate) {
    Args args({"vehicle-sim", "--simulate", "--log-csv", "output.csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.simulate_mode);
    EXPECT_EQ(opts.log_csv, "output.csv");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogCsvWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--log-csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogRawWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--log-raw"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectDemoFlag) {
    Args args({"vehicle-sim", "--connect-demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_demo);
    EXPECT_FALSE(opts.help_requested);
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectDemoWithLogCsv) {
    Args args({"vehicle-sim", "--connect-demo", "--log-csv", "output.csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_demo);
    EXPECT_EQ(opts.log_csv, "output.csv");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectDemoWithLogRaw) {
    Args args({"vehicle-sim", "--connect-demo", "--log-raw", "trace.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.connect_demo);
    EXPECT_EQ(opts.log_raw, "trace.log");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ConnectDemoDefaultsToFalseWhenNotSpecified) {
    Args args({"vehicle-sim"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.connect_demo);
}
