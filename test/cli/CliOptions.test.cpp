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

    EXPECT_FALSE(opts.mode.scan_mode);
    EXPECT_FALSE(opts.mode.help_requested);
    EXPECT_FALSE(opts.mode.list_signals);
    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.update_interval_ms, DEFAULT_UPDATE_INTERVAL_MS);
    EXPECT_EQ(opts.telemetry.format, DEFAULT_FORMAT);
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
    EXPECT_TRUE(opts.telemetry.vehicle_type.empty());
}

TEST_F(CliOptionsTest, HelpFlag) {
    Args args({"vehicle-sim", "--help"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.help_requested);
}

TEST_F(CliOptionsTest, HelpShortFlag) {
    Args args({"vehicle-sim", "-h"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.help_requested);
}

// Help is now derived from the CLI11 registrations, so every registered flag
// (including the WiFi-provisioning ones) must surface in --help. A regression
// that drops an option from parseArgs would now also drop it from help, and
// this test catches it.
TEST_F(CliOptionsTest, HelpTextSurfacesProvisioningFlags) {
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());

    Args args({"vehicle-sim", "--help"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.help_requested);
    ASSERT_FALSE(opts.mode.help_text.empty());

    // The derived help_text must list every registered provisioning flag.
    EXPECT_NE(opts.mode.help_text.find("--set-wifi-creds"), std::string::npos);
    EXPECT_NE(opts.mode.help_text.find("--clear-wifi-creds"), std::string::npos);
    EXPECT_NE(opts.mode.help_text.find("--reboot"), std::string::npos);
    EXPECT_NE(opts.mode.help_text.find("--port"), std::string::npos);

    // printHelp forwards help_text (plus the registry-driven SUPPORTED
    // VEHICLES block), so it must surface the same flags.
    std::ostringstream out;
    printHelp(out, service, opts.mode.help_text);
    const std::string help = out.str();
    EXPECT_NE(help.find("--set-wifi-creds"), std::string::npos);
    EXPECT_NE(help.find("--clear-wifi-creds"), std::string::npos);
    EXPECT_NE(help.find("--reboot"), std::string::npos);
    EXPECT_NE(help.find("--port"), std::string::npos);
}

// printHelp must surface at least one registered vehicle id from the registry.
TEST_F(CliOptionsTest, PrintHelpEmitsRegisteredVehicleId) {
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());

    Args args({"vehicle-sim", "--help"});
    auto opts = parseArgs(args.argc(), args.argv());

    std::ostringstream out;
    printHelp(out, service, opts.mode.help_text);
    const std::string help = out.str();

    auto vehicles = service.registry().getRegisteredVehicles();
    ASSERT_FALSE(vehicles.empty());
    EXPECT_NE(help.find(vehicles.front()), std::string::npos);
    EXPECT_NE(help.find("SUPPORTED VEHICLES"), std::string::npos);
}

TEST_F(CliOptionsTest, ScanFlag) {
    Args args({"vehicle-sim", "--scan"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.scan_mode);
}

TEST_F(CliOptionsTest, ScanShortFlag) {
    Args args({"vehicle-sim", "-s"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.scan_mode);
}

TEST_F(CliOptionsTest, ConnectDemo) {
    Args args({"vehicle-sim", "--connect", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "demo");
    EXPECT_TRUE(opts.isDemo());
    EXPECT_FALSE(opts.isBLE());
}

TEST_F(CliOptionsTest, ConnectDemoShortFlag) {
    Args args({"vehicle-sim", "-c", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.telemetry.connect_target, "demo");
    EXPECT_TRUE(opts.isDemo());
}

TEST_F(CliOptionsTest, ConnectBLEAddress) {
    Args args({"vehicle-sim", "--connect", "AA:BB:CC:DD:EE:FF", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "AA:BB:CC:DD:EE:FF");
    EXPECT_FALSE(opts.isDemo());
    EXPECT_TRUE(opts.isBLE());
    EXPECT_FALSE(opts.isTcp());
    EXPECT_EQ(opts.telemetry.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, ConnectTcpIpPort_IsTcpAndNotBle) {
    Args args({"vehicle-sim", "--connect", "tcp:192.168.4.1:3333", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "tcp:192.168.4.1:3333");
    EXPECT_FALSE(opts.isDemo());
    EXPECT_FALSE(opts.isFile());
    EXPECT_FALSE(opts.isBLE());
    EXPECT_TRUE(opts.isTcp());
}

TEST_F(CliOptionsTest, ConnectTcpIpOnly_IsTcp) {
    Args args({"vehicle-sim", "--connect", "tcp:192.168.4.1", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.isTcp());
    EXPECT_FALSE(opts.isBLE());
}

TEST_F(CliOptionsTest, ConnectTcpWithLogFlags_Parses) {
    Args args({"vehicle-sim", "--connect", "tcp:192.168.4.1:3333", "--vehicle", "tesla",
               "--log-raw", "x.raw", "--log-csv", "x.csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.isTcp());
    EXPECT_EQ(opts.logging.log_raw, "x.raw");
    EXPECT_EQ(opts.logging.log_csv, "x.csv");
}

TEST_F(CliOptionsTest, ConnectDemoWithVehicle) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "demo");
    EXPECT_EQ(opts.telemetry.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, InvalidVehicleTypeParsesWithoutError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "invalid"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.vehicle_type, "invalid");
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
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
}

TEST_F(CliOptionsTest, IntervalOverride) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--interval", "250"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.telemetry.update_interval_ms, 250);
    EXPECT_TRUE(opts.telemetry.update_interval_explicit);
}

TEST_F(CliOptionsTest, IntervalShortFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "-i", "100"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.telemetry.update_interval_ms, 100);
    EXPECT_TRUE(opts.telemetry.update_interval_explicit);
}

// CSV replay paces off the recorded row timestamps by default; the 500ms
// live-emission default (DEFAULT_UPDATE_INTERVAL_MS) must NOT leak into
// replay pacing (a 123k-row capture would replay over ~17 hours).
TEST_F(CliOptionsTest, IntervalExplicitDefaultsToFalse) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.telemetry.update_interval_explicit);
}

TEST_F(CliOptionsTest, IntervalWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--interval"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, FormatOverride) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--format", "json"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.telemetry.format, "json");
}

TEST_F(CliOptionsTest, FormatWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--format"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ListSignalsFlag) {
    Args args({"vehicle-sim", "--list"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.list_signals);
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
    EXPECT_EQ(opts.telemetry.vehicle_type, "tesla");
}

TEST_F(CliOptionsTest, ValidAudiVehicleType) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "audi_mlb_evo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.vehicle_type, "audi_mlb_evo");
}

TEST_F(CliOptionsTest, ValidGenericVehicleType) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "generic"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.vehicle_type, "generic");
}

TEST_F(CliOptionsTest, LogCsvFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-csv", "trace.csv"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.logging.log_csv, "trace.csv");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogRawFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-raw", "raw.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.logging.log_raw, "raw.log");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, BothLogFlags) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log-csv", "trace.csv", "--log-raw", "raw.log"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.logging.log_csv, "trace.csv");
    EXPECT_EQ(opts.logging.log_raw, "raw.log");
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

    EXPECT_TRUE(opts.mode.scan_mode);
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
    EXPECT_TRUE(opts.telemetry.vehicle_type.empty());
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, ListWithoutConnectNoError) {
    Args args({"vehicle-sim", "--list"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.list_signals);
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
    EXPECT_TRUE(opts.telemetry.vehicle_type.empty());
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

TEST_F(CliValidationTest, ValidateVehicleAuto_TcpConnect_ReturnsError) {
    // TCP source has no VIN detection — 'auto' requires BLE.
    Args args({"vehicle-sim", "--connect", "tcp:192.168.4.1:3333", "--vehicle", "auto"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("auto requires a BLE connection"), std::string::npos);
}

TEST_F(CliValidationTest, ValidateTcpConnect_ValidVehicle_NoError) {
    Args args({"vehicle-sim", "--connect", "tcp:192.168.4.1:3333", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
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

// ============================================================
// --log <base> (canonical) and deprecated --log-csv/--log-raw aliases
// ============================================================

TEST_F(CliOptionsTest, LogBaseFlagSetsLogBase) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log", "/tmp/run1"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_EQ(opts.logging.log_base, "/tmp/run1");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, LogBaseWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "--log"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, DeprecatedLogCsvStillParses) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--log-csv", "trace.csv"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_EQ(opts.logging.log_csv, "trace.csv");
    EXPECT_TRUE(opts.logging.log_base.empty());  // canonical unset when alias used
    EXPECT_TRUE(opts.error_message.empty());
}

// ============================================================
// --adapter-protocol raw|elm327
// ============================================================

TEST_F(CliOptionsTest, AdapterProtocolDefaultsToRaw) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_EQ(opts.logging.adapter_protocol, "raw");
}

TEST_F(CliOptionsTest, AdapterProtocolRawParses) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--adapter-protocol", "raw"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_EQ(opts.logging.adapter_protocol, "raw");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, AdapterProtocolElm327Parses) {
    // Parsing accepts 'elm327' (a known value); validation accepts it too.
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--adapter-protocol", "elm327"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_EQ(opts.logging.adapter_protocol, "elm327");
    EXPECT_TRUE(opts.error_message.empty());
}

TEST_F(CliOptionsTest, AdapterProtocolWithoutValueReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--adapter-protocol"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_FALSE(opts.error_message.empty());
}

TEST_F(CliValidationTest, ValidateElm327Protocol_NoError_AcceptedForLaterWiring) {
    // elm327 is now ACCEPTED (no longer "Phase 1 not implemented"). The
    // default table + explicit override are resolved by the pipeline factory;
    // validation only rejects UNKNOWN values.
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--adapter-protocol", "elm327"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
}

TEST_F(CliValidationTest, ValidateUnknownProtocol_ReturnsError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--adapter-protocol", "canbus"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Unknown --adapter-protocol"), std::string::npos);
}

TEST_F(CliValidationTest, ValidateRawProtocol_NoError) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--adapter-protocol", "raw"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
}

// --- WiFi provisioning CLI --------------------------------------------------

TEST_F(CliOptionsTest, SetWifiCreds_ParsesTwoValues) {
    Args args({"vehicle-sim", "--set-wifi-creds", "MyNet", "s3cr3t"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.set_wifi_ssid, "MyNet");
    EXPECT_EQ(opts.wifi.set_wifi_pass, "s3cr3t");
    EXPECT_TRUE(opts.isProvisioning());
    EXPECT_FALSE(opts.wifi.clear_wifi_creds);
    EXPECT_FALSE(opts.wifi.reboot_esp32);
}

TEST_F(CliOptionsTest, SetWifiCreds_RequiresTwoValues) {
    // expected(2) should reject a single positional value as a parse error.
    Args args({"vehicle-sim", "--set-wifi-creds", "OnlySsid"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty());
    EXPECT_FALSE(opts.isProvisioning());
}

TEST_F(CliOptionsTest, ClearWifiCreds_FlagParses) {
    Args args({"vehicle-sim", "--clear-wifi-creds"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.wifi.clear_wifi_creds);
    EXPECT_TRUE(opts.isProvisioning());
}

TEST_F(CliOptionsTest, RebootFlag_Parses) {
    Args args({"vehicle-sim", "--reboot"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.wifi.reboot_esp32);
    EXPECT_TRUE(opts.isProvisioning());
}

TEST_F(CliOptionsTest, ProvisioningExemptsConnectRequirement_Probe) {
    // Provisioning must not be rejected by validateOptions for missing --connect.
    // Uses a locally-built service (this fixture has no service_ member).
    DBCTranslationService svc;
    DefaultVehicleConfigs::registerAll(svc.registry());
    Args args({"vehicle-sim", "--clear-wifi-creds"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, svc);
    EXPECT_TRUE(error.empty());
}

TEST_F(CliOptionsTest, PortFlagOverridesDefault) {
    Args args({"vehicle-sim", "--reboot", "--port", "/dev/cu.usbserial-999"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.usb_port, "/dev/cu.usbserial-999");
}

// The two free-form vehicle-label paths (interactive mode, decoded-CSV replay)
// flow the --vehicle value UNSUBSTITUTED into the CSV DATA sink, so a control
// character in the label would corrupt the emitted row (and is the S5145 taint
// source). validateOptions must REJECT such a label, not silently mangle it.
TEST_F(CliValidationTest, InteractiveMode_RejectsControlCharVehicleLabel) {
    Args args({"vehicle-sim", "--interactive", "--vehicle", std::string("tes\nla")});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("control characters"), std::string::npos);
}

TEST_F(CliValidationTest, InteractiveMode_RejectsDelLabel) {
    // 0x7F (DEL) is also a control byte that must be rejected.
    Args args({"vehicle-sim", "--interactive", "--vehicle", std::string("tes") + '\x7F' + "la"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("control characters"), std::string::npos);
}

// Decoded-CSV replay also lets --vehicle through as a free-form label; the same
// boundary check must apply. validateOptions only inspects the file header
// on-disk, so we point it at a real decoded-CSV file via file:<path>.
TEST_F(CliValidationTest, DecodedCsvReplay_RejectsControlCharVehicleLabel) {
    char ftmpl[] = "/tmp/vsim_hdr_XXXXXX";
    int ffd = mkstemp(ftmpl);
    ASSERT_GE(ffd, 0);
    std::string fpath = ftmpl;
    const char hdr[] = "timestamp_ms,vehicle_id\n";
    ASSERT_EQ(write(ffd, hdr, sizeof(hdr) - 1), static_cast<ssize_t>(sizeof(hdr) - 1));
    close(ffd);

    Args args({"vehicle-sim", "--connect", "file:" + fpath, "--vehicle", std::string("tes\nla")});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    std::remove(fpath.c_str());
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("control characters"), std::string::npos);
}

TEST_F(CliValidationTest, InteractiveMode_RejectsOverlongVehicleLabel) {
    Args args({"vehicle-sim", "--interactive", "--vehicle", std::string(65, 'x')});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("exceeds"), std::string::npos);
}

// A normal (printable, bounded) label must still pass on both free-form paths.
TEST_F(CliValidationTest, InteractiveMode_AcceptsNormalVehicleLabel) {
    Args args({"vehicle-sim", "--interactive", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty());
}

