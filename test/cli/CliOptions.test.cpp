#include <gtest/gtest.h>
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "test/cli/Args.h"

#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace vehicle_sim::cli;
using namespace vehicle_sim::domain;

class CliOptionsTest : public ::testing::Test {};

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
    Args args({"vehicle-sim", "--help"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.help_requested);
    ASSERT_FALSE(opts.mode.help_text.empty());

    // The derived help_text must list every registered provisioning flag.
    EXPECT_NE(opts.mode.help_text.find("--set-wifi-creds"), std::string::npos);
    EXPECT_NE(opts.mode.help_text.find("--clear-wifi-creds"), std::string::npos);
    EXPECT_NE(opts.mode.help_text.find("--reboot"), std::string::npos);
    EXPECT_NE(opts.mode.help_text.find("--status"), std::string::npos);

    // printHelp forwards help_text verbatim, so it must surface the same flags.
    std::ostringstream out;
    printHelp(out, opts.mode.help_text);
    const std::string help = out.str();
    EXPECT_NE(help.find("--set-wifi-creds"), std::string::npos);
    EXPECT_NE(help.find("--clear-wifi-creds"), std::string::npos);
    EXPECT_NE(help.find("--reboot"), std::string::npos);
    EXPECT_NE(help.find("--status"), std::string::npos);

    // --port has been removed (the USB serial port is now a build-time
    // default; --connect is the universal transport selector); the help
    // must no longer mention --port.
    EXPECT_EQ(help.find("--port"), std::string::npos);

    // The EXAMPLES / NOTES / REQUIREMENTS footer is no longer part of --help
    // (it lives on --examples, and notes are folded into option descriptions).
    // This is the single-screen guarantee the user asked for.
    EXPECT_EQ(help.find("EXAMPLES:"), std::string::npos);
    EXPECT_EQ(help.find("NOTES:"), std::string::npos);
    EXPECT_EQ(help.find("REQUIREMENTS:"), std::string::npos);
}

// The SUPPORTED VEHICLES block is --list's job, not --help's. A regression
// that re-adds it to printHelp would duplicate the list (it's also emitted
// by printSupportedSignals, which --list routes to).
TEST_F(CliOptionsTest, HelpDoesNotEmitSupportedVehicles) {
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());

    Args args({"vehicle-sim", "--help"});
    auto opts = parseArgs(args.argc(), args.argv());

    std::ostringstream out;
    printHelp(out, opts.mode.help_text);
    const std::string help = out.str();
    EXPECT_EQ(help.find("SUPPORTED VEHICLES"), std::string::npos);
}

// --examples captures the curated EXAMPLES block into opts.mode.examples_text
// and routes through printExamples(), which the orchestrator dispatches to.
// The payload is the embedded copy of assets/examples.md (single source of
// truth) — see ExamplesContent.h.
TEST_F(CliOptionsTest, ExamplesFlagCapturesText) {
    Args args({"vehicle-sim", "--examples"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.mode.examples_requested);
    EXPECT_TRUE(opts.mode.help_requested);  // also short-circuits like --help
    ASSERT_FALSE(opts.mode.examples_text.empty());
    // The block must mention a representative example for every key topic
    // (curated; the focus-filter tests below exercise the topic grammar).
    EXPECT_NE(opts.mode.examples_text.find("vehicle-sim --connect demo"), std::string::npos);
    EXPECT_NE(opts.mode.examples_text.find("vehicle-sim --set-wifi-creds"), std::string::npos);
    // Section headings are embedded as `# section: NAME` markers in the
    // asset; their rendered names appear in the printed output.
    EXPECT_NE(opts.mode.examples_text.find("# section: CONNECTIONS"), std::string::npos);
    EXPECT_NE(opts.mode.examples_text.find("# section: WIFI SETUP"), std::string::npos);
    EXPECT_NE(opts.mode.examples_text.find("# section: LOGGING"), std::string::npos);
    EXPECT_NE(opts.mode.examples_text.find("# section: OUTPUT"), std::string::npos);
    // Trivial single-flag topics (--discover, --scan, --list, --led-help)
    // are intentionally absent from the curated payload — the user already
    // knows how to use them from the help text.
    EXPECT_EQ(opts.mode.examples_text.find("# topic: discover"), std::string::npos);
    EXPECT_EQ(opts.mode.examples_text.find("# topic: scan"), std::string::npos);
    EXPECT_EQ(opts.mode.examples_text.find("# topic: list"), std::string::npos);
    EXPECT_EQ(opts.mode.examples_text.find("# topic: led-help"), std::string::npos);
}

// A topic tag with a trailing comma (or an all-whitespace segment) produces
// an empty segment after the comma-split. The parser must skip it rather than
// emplacing an empty topic name — that empty-segment branch is the regression
// target (cpp:S134 nesting at the segLead/segTrail check). The non-empty
// segment still parses and renders.
TEST_F(CliOptionsTest, PrintExamplesSkipsEmptyTopicSegment) {
    const std::string sample =
        "# section: CONNECTIONS\n"
        "\n"
        "# topic: connect,   \n"
        "  vehicle-sim --connect demo --vehicle tesla\n";

    std::ostringstream out;
    printExamples(out, sample, {});
    const std::string text = out.str();
    EXPECT_NE(text.find("CONNECTIONS:"), std::string::npos);
    EXPECT_NE(text.find("--connect demo"), std::string::npos);
    // The empty segment must NOT surface as a bare topic name in the output.
    EXPECT_EQ(text.find("  \n"), std::string::npos);
}

// printExamples with empty focus shows the full block, grouped by section
// headings. The renderer prints the heading once above its matching blocks;
// a focus-filtered render still emits the heading for any section that
// retained at least one block.
TEST_F(CliOptionsTest, PrintExamplesUnfiltered_ShowsAllTopics) {
    Args args({"vehicle-sim", "--examples"});
    auto opts = parseArgs(args.argc(), args.argv());

    std::ostringstream out;
    printExamples(out, opts.mode.examples_text, {});
    const std::string text = out.str();
    EXPECT_NE(text.find("EXAMPLES:"), std::string::npos);
    EXPECT_NE(text.find("--connect demo"), std::string::npos);
    EXPECT_NE(text.find("--set-wifi-creds"), std::string::npos);
    EXPECT_NE(text.find("--clear-wifi-creds"), std::string::npos);
    // Section headings render as `NAME:` (no `#` prefix). The user asked
    // for subtitles; this is the regression guard.
    EXPECT_NE(text.find("CONNECTIONS:"), std::string::npos);
    EXPECT_NE(text.find("WIFI SETUP:"), std::string::npos);
    EXPECT_NE(text.find("LOGGING:"), std::string::npos);
    EXPECT_NE(text.find("OUTPUT:"), std::string::npos);
    // Trivial single-flag topics are absent even from the unfiltered
    // render — the user doesn't need a "how to use --scan" example.
    EXPECT_EQ(text.find("--discover\n"), std::string::npos);
    EXPECT_EQ(text.find("--scan\n"), std::string::npos);
    EXPECT_EQ(text.find("--led-help\n"), std::string::npos);
}

// `--help --connect` populates help_focus with "connect"; printExamples
// filters to the connect topic and its children (connect-usb, connect-tcp,
// connect-auto, connect-file). The synthetic sample below uses the section
// markers too so the test covers the new parser grammar.
TEST_F(CliOptionsTest, PrintExamplesFiltered_FocusConnect) {
    Args args({"vehicle-sim", "--help", "--connect"});
    auto opts = parseArgs(args.argc(), args.argv());

    ASSERT_TRUE(opts.mode.help_requested);
    EXPECT_FALSE(opts.mode.help_focus.empty());
    EXPECT_EQ(opts.mode.help_focus.front(), "connect");

    // examples_text is empty on --help (it's only captured on --examples).
    // We synthesise a tiny block to test the focus filter end-to-end.
    const std::string sample =
        "# section: CONNECTIONS\n"
        "\n"
        "# topic: connect\n"
        "  vehicle-sim --connect demo --vehicle tesla\n"
        "\n"
        "# topic: connect-usb\n"
        "  vehicle-sim --connect-usb /dev/cu.usbserial-110 --vehicle tesla\n"
        "\n"
        "# section: WIFI SETUP\n"
        "\n"
        "# topic: reboot\n"
        "  vehicle-sim --reboot --connect auto\n"
        "\n"
        "# section: LOGGING\n"
        "\n"
        "# topic: scan\n"
        "  vehicle-sim --scan\n";

    std::ostringstream out;
    printExamples(out, sample, opts.mode.help_focus);
    const std::string text = out.str();
    EXPECT_NE(text.find("--connect demo"), std::string::npos);
    EXPECT_NE(text.find("--connect-usb"), std::string::npos);
    // The CONNECTIONS heading survives (it has matching blocks). The
    // WIFI SETUP and LOGGING headings must NOT appear (no matches under
    // them after the focus filter).
    EXPECT_NE(text.find("CONNECTIONS:"), std::string::npos);
    EXPECT_EQ(text.find("WIFI SETUP:"), std::string::npos);
    EXPECT_EQ(text.find("LOGGING:"), std::string::npos);
    // scan must NOT appear when focus is "connect".
    EXPECT_EQ(text.find("--scan"), std::string::npos);
    EXPECT_EQ(text.find("--reboot --connect auto"), std::string::npos);
}

// `--help --reboot` filters to just the reboot examples (and its section
// heading). The synthesis covers the section-aware filter.
TEST_F(CliOptionsTest, PrintExamplesFiltered_FocusReboot) {
    Args args({"vehicle-sim", "--help", "--reboot"});
    auto opts = parseArgs(args.argc(), args.argv());

    ASSERT_TRUE(opts.mode.help_requested);
    EXPECT_EQ(opts.mode.help_focus.front(), "reboot");

    const std::string sample =
        "# section: CONNECTIONS\n"
        "\n"
        "# topic: connect\n"
        "  vehicle-sim --connect demo --vehicle tesla\n"
        "\n"
        "# section: WIFI SETUP\n"
        "\n"
        "# topic: reboot\n"
        "  vehicle-sim --reboot --connect auto\n";

    std::ostringstream out;
    printExamples(out, sample, opts.mode.help_focus);
    const std::string text = out.str();
    EXPECT_NE(text.find("WIFI SETUP:"), std::string::npos);
    EXPECT_NE(text.find("--reboot --connect auto"), std::string::npos);
    // The CONNECTIONS heading and its blocks must NOT appear.
    EXPECT_EQ(text.find("CONNECTIONS:"), std::string::npos);
    EXPECT_EQ(text.find("--connect demo"), std::string::npos);
}

// `--examples --foo` is an UNKNOWN option. The user flagged this as the
// worst possible UX (silent exit 0 with an empty EXAMPLES block). parseArgs
// must now surface the parse error so main.cpp exits 1.
TEST_F(CliOptionsTest, ExamplesUnknownArgIsRejected) {
    Args args({"vehicle-sim", "--examples", "--foo"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty())
        << "--examples --foo must be rejected, not silently swallowed";
    EXPECT_NE(opts.error_message.find("--foo"), std::string::npos);
    // The EXAMPLES payload must NOT be set on a failed parse — the
    // orchestrator is going to short-circuit on the error before printing
    // anything, and an empty payload would be confusing.
    EXPECT_TRUE(opts.mode.examples_text.empty());
}

// `--examples connect` (positional-style) and `--examples --connect` must
// both filter to the connect topic after the swallow-conditional fix above.
TEST_F(CliOptionsTest, ExamplesFocus_FilterStillWorks_AfterUnknownArgFix) {
    // Bare topic name as a positional arg.
    {
        Args args({"vehicle-sim", "--examples", "connect"});
        auto opts = parseArgs(args.argc(), args.argv());
        EXPECT_TRUE(opts.error_message.empty());
        ASSERT_TRUE(opts.mode.examples_requested);
        ASSERT_FALSE(opts.mode.help_focus.empty());
        EXPECT_EQ(opts.mode.help_focus.front(), "connect");
    }
    // --<topic> with leading dashes — stripLeadingDashes turns it into
    // "connect" too.
    {
        Args args({"vehicle-sim", "--examples", "--connect"});
        auto opts = parseArgs(args.argc(), args.argv());
        EXPECT_TRUE(opts.error_message.empty());
        ASSERT_TRUE(opts.mode.examples_requested);
        ASSERT_FALSE(opts.mode.help_focus.empty());
        EXPECT_EQ(opts.mode.help_focus.front(), "connect");
    }
}

// computeHelpFocus() must skip the value that follows a value-taking option so
// `--help --connect demo` yields focus ["connect"], NOT ["connect", "demo"].
// The value-skip branch (the `++i` inside the `if (i + 1 < argc)` block) is
// the regression target: without it, a value that legitimately follows a
// value-taking option would be misread as a focus token.
TEST_F(CliOptionsTest, HelpFocus_SkipsValueFollowingValueTakingOption) {
    Args args({"vehicle-sim", "--help", "--connect", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    ASSERT_TRUE(opts.mode.help_requested);
    ASSERT_EQ(opts.mode.help_focus.size(), 1u);
    EXPECT_EQ(opts.mode.help_focus.front(), "connect");
}

// A bare focus name followed by a value is also skipped (the same branch, but
// the token has no leading dashes). `--help connect demo` → focus ["connect"].
TEST_F(CliOptionsTest, HelpFocus_SkipsValueFollowingBareFocusToken) {
    Args args({"vehicle-sim", "--help", "connect", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    ASSERT_TRUE(opts.mode.help_requested);
    ASSERT_EQ(opts.mode.help_focus.size(), 1u);
    EXPECT_EQ(opts.mode.help_focus.front(), "connect");
}

// `printExamples` with empty payload still emits a header (regression guard
// for the early-exit orchestrator path which always calls printExamples
// when --examples was requested, even if the focus filter ends up matching
// nothing).
TEST_F(CliOptionsTest, PrintExamplesWithEmptyText_EmitsHeaderOnly) {
    std::ostringstream out;
    printExamples(out, "", {});
    const std::string text = out.str();
    EXPECT_NE(text.find("EXAMPLES:"), std::string::npos);
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

TEST_F(CliOptionsTest, ConnectTcpWithLogFlag_Parses) {
    // The canonical --log <base> is the only logging flag. Earlier drafts
    // also accepted --log-raw / --log-csv as deprecated aliases; those were
    // removed. This test exercises the surviving --log form.
    Args args({"vehicle-sim", "--connect", "tcp:192.168.4.1:3333", "--vehicle", "tesla",
               "--log", "captures/run1"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.isTcp());
    EXPECT_EQ(opts.logging.log_base, "captures/run1");
}

TEST_F(CliOptionsTest, DeprecatedLogFlagsAreRejected) {
    // --log-raw / --log-csv are no longer registered; they must fail parsing
    // as unknown options. Regression guard: a future refactor that re-adds
    // them as aliases must update this test, not silently bring them back.
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla",
               "--log-raw", "x.raw"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty())
        << "--log-raw must now be rejected as an unknown option";
    EXPECT_NE(opts.error_message.find("--log-raw"), std::string::npos);
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
}

TEST_F(CliOptionsTest, IntervalShortFlag) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla", "-i", "100"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_EQ(opts.telemetry.update_interval_ms, 100);
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

// --log-csv / --log-raw were removed: the canonical --log <base> is the
// only logging flag. Their dedicated tests are gone; see
// DeprecatedLogFlagsAreRejected above for the negative-coverage guard.

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
// --log <base> (canonical) — deprecated --log-csv / --log-raw removed
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

// --- Provisioning transport validation: usb/auto/tcp are interchangeable ----

// A well-formed tcp: target validates for EVERY provisioning command. These
// are the exact invocations the bug report used.
TEST_F(CliValidationTest, ProvisioningTcpTransport_Valid_HostPort) {
    Args args({"vehicle-sim", "--set-wifi-creds", "manht2", "s3cr3t",
               "--connect", "tcp:192.168.68.91:3333"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_TRUE(validateOptions(opts, service_).empty());
}

TEST_F(CliValidationTest, ProvisioningTcpTransport_Valid_DefaultPort) {
    Args args({"vehicle-sim", "--status", "--connect", "tcp:192.168.68.91"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_TRUE(validateOptions(opts, service_).empty());
}

TEST_F(CliValidationTest, ProvisioningTcpTransport_Valid_StatusAlias) {
    Args args({"vehicle-sim", "--status", "--connect-tcp", "192.168.68.91:3333"});
    auto opts = parseArgs(args.argc(), args.argv());
    EXPECT_TRUE(validateOptions(opts, service_).empty());
}

// Malformed tcp: targets get a clear, tcp-specific validation error from the
// ONE validation site (the canonical parser decides what is well-formed).
TEST_F(CliValidationTest, ProvisioningTcpTransport_EmptyHost_Rejected) {
    Args args({"vehicle-sim", "--clear-wifi-creds", "--connect", "tcp:"});
    auto opts = parseArgs(args.argc(), args.argv());

    const auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Invalid TCP provisioning target"), std::string::npos);
    EXPECT_NE(error.find("tcp:"), std::string::npos);
}

TEST_F(CliValidationTest, ProvisioningTcpTransport_ZeroPort_Rejected) {
    Args args({"vehicle-sim", "--reboot", "--connect", "tcp:host:0"});
    auto opts = parseArgs(args.argc(), args.argv());

    const auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Invalid TCP provisioning target"), std::string::npos);
}

TEST_F(CliValidationTest, ProvisioningTcpTransport_OutOfRangePort_Rejected) {
    Args args({"vehicle-sim", "--reboot", "--connect", "tcp:host:99999"});
    auto opts = parseArgs(args.argc(), args.argv());

    const auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Invalid TCP provisioning target"), std::string::npos);
}

// The usb:/auto forms are unchanged, and non-transport values are still
// rejected — with guidance that now includes the tcp: form.
TEST_F(CliValidationTest, ProvisioningUsbAndAuto_StillValid) {
    {
        Args args({"vehicle-sim", "--clear-wifi-creds", "--connect",
                   "usb:/dev/cu.usbserial-110"});
        auto opts = parseArgs(args.argc(), args.argv());
        EXPECT_TRUE(validateOptions(opts, service_).empty());
    }
    {
        Args args({"vehicle-sim", "--reboot", "--connect", "auto"});
        auto opts = parseArgs(args.argc(), args.argv());
        EXPECT_TRUE(validateOptions(opts, service_).empty());
    }
}

TEST_F(CliValidationTest, ProvisioningNonTransportTarget_StillRejected) {
    Args args({"vehicle-sim", "--clear-wifi-creds", "--connect", "demo"});
    auto opts = parseArgs(args.argc(), args.argv());

    const auto error = validateOptions(opts, service_);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("is not supported"), std::string::npos);
    EXPECT_NE(error.find("tcp:<host>[:<port>]"), std::string::npos)
        << "the guidance must now include the tcp: form";
}

// --port has been intentionally removed (the USB serial port is now a
// build-time default; an explicit --port override was redundant and was the
// source of more "which port did I pick again?" confusion than it solved).
// The flag is now an unknown option; the way to select a specific port is
// --connect usb:/dev/cu.usbserial-XXX.
TEST_F(CliOptionsTest, PortFlagIsRemoved) {
    Args args({"vehicle-sim", "--reboot", "--port", "/dev/cu.usbserial-999"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_FALSE(opts.error_message.empty())
        << "--port should now be rejected as an unknown option";
    EXPECT_NE(opts.error_message.find("--port"), std::string::npos);
}

TEST_F(CliOptionsTest, SetWifiCreds_ConnectUsb_FlowsIntoSerialPort) {
    Args args({"vehicle-sim", "--set-wifi-creds", "MyNet", "s3cr3t",
               "--connect", "usb:/dev/cu.usbserial-777"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.isProvisioning());
    EXPECT_EQ(opts.wifi.transport, "usb:/dev/cu.usbserial-777");
    EXPECT_TRUE(opts.telemetry.connect_target.empty())
        << "a provisioning --connect must be consumed by the provisioner, "
           "not left as a telemetry target";
}

TEST_F(CliOptionsTest, ClearWifiCreds_ConnectUsb_FlowsIntoSerialPort) {
    Args args({"vehicle-sim", "--clear-wifi-creds", "--connect", "usb:/dev/cu.SLAB_USBtoUART"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.transport, "usb:/dev/cu.SLAB_USBtoUART");
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
}

// NOTE (merged from the startStop track): the parse-time ESP32_PORT env-var
// fallback and the WifiProvisioningOptions::usb_port field it populated were
// superseded by the origin/master resolver design — the Makefile now always
// passes --connect "usb:$(ESP32_PORT)" explicitly, and a missing --connect
// triggers the provisioner's run-time /dev/cu.* auto-detect
// (resolveSerialPort, ProvisioningRunner). The env-behaviour tests that
// asserted the old parse-time fold were removed with it; the no-connect
// auto-detect contract is covered by
// ProvisioningWithoutConnectLeavesTransportEmpty below.

// ============================================================
// --connect usb:<path> — the live USB path, end to end through the REAL
// option-parsing. A pseudo-terminal stands in for the /dev/cu.* device:
// parseArgs must yield a usb: target, and buildPipelineSource (the same
// factory LiveRunContext uses) must turn it into a transport that actually
// OPENS the serial device and streams lines from it. This is the regression
// net for "usb: has worked for ages" — it pins the whole chain in ctest.
// ============================================================

namespace {

class PtyPair {
public:
    PtyPair() {
        if (::openpty(&masterFd_, &slaveFd_, slaveName_, nullptr, nullptr) != 0) {
            slaveFd_ = -1;
            masterFd_ = -1;
        }
    }
    ~PtyPair() {
        if (slaveFd_ >= 0) ::close(slaveFd_);
        if (masterFd_ >= 0) ::close(masterFd_);
    }
    PtyPair(const PtyPair&) = delete;
    PtyPair& operator=(const PtyPair&) = delete;
    [[nodiscard]] bool valid() const {
        return masterFd_ >= 0 && slaveFd_ >= 0 && slaveName_[0] != '\0';
    }
    [[nodiscard]] const char* devicePath() const { return slaveName_; }
    [[nodiscard]] int masterFd() const { return masterFd_; }

private:
    int masterFd_ = -1;
    int slaveFd_ = -1;
    char slaveName_[128]{};
};

} // namespace

TEST_F(CliOptionsTest, ConnectUsb_OpensSerialTransportViaRealOptionParsing) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    const std::string target = std::string("usb:") + pty.devicePath();
    Args args({"vehicle-sim", "--connect", target, "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    // The real parsing path: no error, a usb: target, validation clean.
    ASSERT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.isUsb());
    EXPECT_EQ(opts.telemetry.connect_target, target);

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    EXPECT_TRUE(validateOptions(opts, service).empty());

    // The same factory call LiveRunContext::run makes.
    auto stop = std::make_shared<vehicle_sim::pipeline::StopToken>();
    auto source = vehicle_sim::pipeline::buildPipelineSource(
        opts.telemetry.connect_target, "raw", stop);
    ASSERT_TRUE(source.transport);

    // The transport must OPEN the PTY device and stream its lines.
    ASSERT_TRUE(source.transport->open());
    ASSERT_TRUE(source.transport->isOpen());

    const std::string frame = "1D5 29 00 00 00 00 00 A0 9F\r";
    ASSERT_EQ(::write(pty.masterFd(), frame.data(), frame.size()),
              static_cast<ssize_t>(frame.size()));
    auto line = source.transport->nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "1D5 29 00 00 00 00 00 A0 9F");
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

// isDecodedTelemetryCsv() returns false when the file can't be opened, so a
// non-existent file is treated as "not a decoded CSV" and the free-form label
// boundary check is skipped. The !in.is_open() failure branch is the
// regression target. With a registered vehicle (which passes validateVehicle),
// validation must succeed — proving the file was NOT routed through the
// decoded-CSV label check.
TEST_F(CliValidationTest, NonExistentFile_TreatedAsNonDecodedCsv) {
    const std::string nonexistent = "/tmp/vsim_does_not_exist_" + std::to_string(getpid()) + ".csv";
    Args args({"vehicle-sim", "--connect", "file:" + nonexistent, "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());
    auto error = validateOptions(opts, service_);
    EXPECT_TRUE(error.empty())
        << "a non-existent file must be treated as a non-decoded CSV; with a "
           "registered vehicle the validation must pass";
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

// ============================================================
// --connect-{usb,tcp,ble,auto} alias folding + provisioning transport fold
// ============================================================

// --connect-usb <path> is sugar for "--connect usb:<path>". The alias
// captures the path tail (no prefix) and parseArgs() folds it onto
// connect_target so every downstream consumer (isUsb(), main.cpp) sees a
// single source of truth. Without the folding we'd ship two fields carrying
// the same value, and any consumer reading the wrong one would diverge.
TEST_F(CliOptionsTest, ConnectUsbAliasFoldsOntoConnectTarget) {
    Args args({"vehicle-sim", "--connect-usb", "/dev/cu.usbserial-X1", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "usb:/dev/cu.usbserial-X1");
    EXPECT_TRUE(opts.isUsb());
    EXPECT_FALSE(opts.isTcp());
    EXPECT_FALSE(opts.isAuto());
}

// --connect-tcp <ip:port> is sugar for "--connect tcp:<ip:port>". The tail
// already includes the colon, so the folding just prepends the prefix.
TEST_F(CliOptionsTest, ConnectTcpAliasFoldsOntoConnectTarget) {
    Args args({"vehicle-sim", "--connect-tcp", "192.168.4.1:3333", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "tcp:192.168.4.1:3333");
    EXPECT_TRUE(opts.isTcp());
    EXPECT_FALSE(opts.isUsb());
}

// --connect-tcp without a port is also accepted (the runtime resolver
// defaults to port 3333); the folding must NOT add a phantom port.
TEST_F(CliOptionsTest, ConnectTcpAliasIpOnlyFoldsVerbatim) {
    Args args({"vehicle-sim", "--connect-tcp", "192.168.4.1", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "tcp:192.168.4.1");
    EXPECT_TRUE(opts.isTcp());
}

// --connect-auto is a marker flag (no value). The folding just stamps
// connect_target to the literal "auto".
TEST_F(CliOptionsTest, ConnectAutoAliasFoldsOntoConnectTarget) {
    Args args({"vehicle-sim", "--connect-auto", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "auto");
    EXPECT_TRUE(opts.isAuto());
    EXPECT_FALSE(opts.isUsb());
    EXPECT_FALSE(opts.isTcp());
}

// --connect-ble is a marker only — the BLE address is still supplied via
// --connect. With only the marker set, connect_target stays empty (and
// validateOptions flags the missing --connect address).
TEST_F(CliOptionsTest, ConnectBleMarkerAloneLeavesTargetEmpty) {
    Args args({"vehicle-sim", "--connect-ble", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.telemetry.connect_target.empty())
        << "the marker alone is not a transport — the address must come via --connect";
    EXPECT_TRUE(opts.telemetry.connect_ble);
}

// --connect-ble + --connect <addr> folds the address into connect_target;
// the marker is preserved on connect_ble so downstream consumers that
// branch on the marker (e.g. BLE-specific protocol hints) still fire.
TEST_F(CliOptionsTest, ConnectBleMarkerWithAddressKeepsBothFields) {
    Args args({"vehicle-sim", "--connect-ble", "--connect", "AA:BB:CC:DD:EE:FF",
               "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "AA:BB:CC:DD:EE:FF");
    EXPECT_TRUE(opts.telemetry.connect_ble);
    EXPECT_TRUE(opts.isBLE());
}

// --connect (canonical) takes precedence over any alias. If both --connect
// and --connect-usb are given, the canonical --connect value wins and the
// alias is ignored — the user explicitly chose the canonical form.
TEST_F(CliOptionsTest, CanonicalConnectBeatsAlias) {
    Args args({"vehicle-sim", "--connect", "demo", "--connect-usb",
               "/dev/cu.usbserial-IGNORED", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "demo");
    EXPECT_TRUE(opts.isDemo());
    EXPECT_NE(opts.telemetry.connect_usb.find("IGNORED"), std::string::npos)
        << "the alias was captured into connect_usb but did NOT overwrite "
           "the canonical --connect — the canonical value wins";
}

// ============================================================
// Universal --connect transport → provisioning.transport fold
// ============================================================

// When a provisioning flag is set, --connect selects the PROVISIONING
// transport (not the telemetry transport). The parsed value is moved from
// telemetry.connect_target onto wifi.transport so the dispatcher
// (runProvisioning) sees a single source of truth, and any downstream
// telemetry dispatcher sees "no telemetry requested" (empty target).
TEST_F(CliOptionsTest, ProvisioningFoldsConnectOntoWifiTransport) {
    Args args({"vehicle-sim", "--set-wifi-creds", "MyNet", "s3cr3t",
               "--connect", "usb:/dev/cu.usbserial-110"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.transport, "usb:/dev/cu.usbserial-110")
        << "--connect must be folded onto wifi.transport when provisioning is active";
    EXPECT_TRUE(opts.telemetry.connect_target.empty())
        << "the moved value must NOT also remain on telemetry.connect_target";
    EXPECT_TRUE(opts.isProvisioning());
}

// Same fold for --connect-auto: the alias folds to "auto" on
// connect_target, then the provisioning-fold moves it to wifi.transport.
TEST_F(CliOptionsTest, ProvisioningFoldsConnectAutoOntoWifiTransport) {
    Args args({"vehicle-sim", "--reboot", "--connect-auto"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.transport, "auto");
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
    EXPECT_TRUE(opts.isProvisioning());
}

// Same fold for --connect-usb (shorthand): the alias folds to
// "usb:<path>" on connect_target, then the provisioning-fold moves it.
TEST_F(CliOptionsTest, ProvisioningFoldsConnectUsbAliasOntoWifiTransport) {
    Args args({"vehicle-sim", "--clear-wifi-creds", "--connect-usb",
               "/dev/cu.usbserial-110"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.transport, "usb:/dev/cu.usbserial-110");
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
}

// Same fold for --connect-tcp: the alias MERELY composes the canonical
// "tcp:<host>:<port>" string — it is not a transport branch of its own. The
// exact invocation the bug report used must land on wifi.transport as a
// well-formed tcp: target.
TEST_F(CliOptionsTest, ProvisioningFoldsConnectTcpAliasOntoWifiTransport) {
    Args args({"vehicle-sim", "--set-wifi-creds", "manht2", "s3cr3t",
               "--connect-tcp", "192.168.68.91:3333"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.transport, "tcp:192.168.68.91:3333")
        << "the alias must compose the same tcp: string --connect would";
    EXPECT_TRUE(opts.telemetry.connect_target.empty())
        << "the moved value must NOT also remain on telemetry.connect_target";
    EXPECT_TRUE(opts.isProvisioning());
}

// The canonical --connect tcp:<host>[:<port>] form folds identically for
// provisioning (default-port form included).
TEST_F(CliOptionsTest, ProvisioningFoldsConnectTcpOntoWifiTransport) {
    Args args({"vehicle-sim", "--status", "--connect", "tcp:192.168.68.91"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.wifi.transport, "tcp:192.168.68.91");
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
    EXPECT_TRUE(opts.wifi.status_requested);
}

// When --status is requested WITHOUT --connect, the transport is left
// empty (NOT "auto") so the resolver's auto-detect leg runs. The "no
// transport" signal is the empty string, not the literal "auto".
TEST_F(CliOptionsTest, ProvisioningWithoutConnectLeavesTransportEmpty) {
    Args args({"vehicle-sim", "--status"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.wifi.transport.empty())
        << "empty transport is the 'auto-detect' signal for the resolver";
    EXPECT_TRUE(opts.isProvisioning());
    EXPECT_TRUE(opts.wifi.status_requested);
}

// The provisioning-fold ONLY runs when a provisioning flag is set; a
// pure-telemetry invocation with --connect must leave the value on
// telemetry.connect_target and leave wifi.transport empty.
TEST_F(CliOptionsTest, TelemetryAloneDoesNotMoveConnectToWifi) {
    Args args({"vehicle-sim", "--connect", "demo", "--vehicle", "tesla"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_EQ(opts.telemetry.connect_target, "demo");
    EXPECT_TRUE(opts.wifi.transport.empty());
    EXPECT_FALSE(opts.isProvisioning());
}

// --status is recognised as a provisioning flag — without it, the same
// --connect value is telemetry, not provisioning. This is the regression
// guard: someone adding a new --status-style flag must remember to add
// it to WifiProvisioningOptions::active() too.
TEST_F(CliOptionsTest, StatusFlagIsProvisioning) {
    Args args({"vehicle-sim", "--status", "--connect", "auto"});
    auto opts = parseArgs(args.argc(), args.argv());

    EXPECT_TRUE(opts.error_message.empty());
    EXPECT_TRUE(opts.wifi.status_requested);
    EXPECT_TRUE(opts.isProvisioning());
    EXPECT_EQ(opts.wifi.transport, "auto")
        << "the provisioning-fold MUST route --connect to wifi.transport";
    EXPECT_TRUE(opts.telemetry.connect_target.empty());
}

