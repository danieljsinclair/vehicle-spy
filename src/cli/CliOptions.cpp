#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/cli/CsvReplayPath.h"
#include "vehicle-sim/cli/ExamplesContent.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "StatusLEDRenderer.h"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace vehicle_sim::cli {

// ===== parseArgs: SRP helpers ===============================================
// Each helper has a single responsibility so parseArgs() stays a thin composer.
// The helpers live in the anonymous namespace because they are pure machinery
// for option-parsing and not part of the public surface.

namespace {

// True if `key` appears verbatim in argv[1..argc-1].
bool argvContains(int argc, char* argv[], std::string_view key) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == key) return true;
    }
    return false;
}

// Register every CLI11 option on `app`. Pure description: no parsing, no
// folding. The OPTIONS list rendered by `app.help()` is derived from these
// registrations, so every option below is automatically present in --help by
// construction.
void registerOptions(CLI::App& app, CliOptions& opts,
                     std::vector<std::string>& setWifiArgs) {
    app.add_flag("-s,--scan", opts.mode.scan_mode, "Scan for BLE OBD2 adapters");
    app.add_flag("-l,--list", opts.mode.list_signals, "List supported signals for each vehicle");
    app.add_flag("--discover", opts.mode.discover_mode, "Discover ESP32 devices on the network via UDP broadcast");
    app.add_flag("--led-help", opts.mode.led_help, "Show StatusLED pattern reference guide");
    app.add_flag("--examples", opts.mode.examples_requested,
                 "Show curated usage examples (use --help --<option> for "
                 "context-filtered examples, e.g. --help --connect)");

    // --connect is the universal transport selector. It applies to BOTH
    // telemetry (--connect demo / --connect tcp:... / --connect auto) AND
    // provisioning (--connect auto / --connect usb:/dev/cu...). parseArgs()
    // routes the value to the correct consumer based on which flags are set
    // (see foldProvisioningTransport() below).
    //
    // --connect-{usb,ble,tcp,auto} are sugar that fill in the transport
    // prefix the user would otherwise have to type as 'usb:'. The remaining
    // --connect forms (demo / file:<path> / BLE address) keep --connect.
    app.add_option("-c,--connect", opts.telemetry.connect_target,
                   "Connect target (telemetry OR provisioning): 'demo', "
                   "'file:<path>', 'tcp:<ip>:<port>', 'usb:<path>', 'auto', "
                   "or BLE adapter address. For provisioning, 'auto', "
                   "'usb:<path>', and 'tcp:<ip>[:<port>]' are the meaningful "
                   "values")
        ->expected(1);
    app.add_option("--connect-usb", opts.telemetry.connect_usb,
                   "Shortcut for '--connect usb:<path>'. Streams live CAN frames "
                   "from an ESP32 CAN-bridge over USB serial (or provisions "
                   "the device at the given USB serial port)")
        ->expected(1);
    app.add_flag("--connect-ble", opts.telemetry.connect_ble,
                 "Shortcut for '--connect <BLE address>'. The address is supplied "
                 "via --connect (this flag asserts a BLE-style target is wanted)");
    app.add_option("--connect-tcp", opts.telemetry.connect_tcp,
                   "Shortcut for '--connect tcp:<ip>[:<port>]>'. Streams live CAN "
                   "frames from an ESP32 CAN-bridge over WiFi, or provisions "
                   "the device at the given host (default port 3333)")
        ->expected(1);
    app.add_flag("--connect-auto", opts.telemetry.connect_auto,
                 "Shortcut for '--connect auto'. Auto-discover an ESP32 on the "
                 "UDP discovery port (telemetry) or auto-detect the USB serial "
                 "port (provisioning)");
    app.add_option("--connect-file", opts.telemetry.connect_file,
                   "Synonym for '--connect file:<path>'. Equivalent to "
                   "--connect file:PATH; PATH is used verbatim (relative to CWD)")
        ->expected(1);
    app.add_option("-v,--vehicle", opts.telemetry.vehicle_type, "Vehicle type (required)")
        ->expected(1);
    app.add_option("-f,--format", opts.telemetry.format, "Output format: json, csv, or plain")
        ->expected(1)
        ->capture_default_str();
    app.add_option("-i,--interval", opts.telemetry.update_interval_ms, "Update interval in milliseconds")
        ->expected(1)
        ->capture_default_str()
        ->check(CLI::Range(0, 60000));
    // Canonical logging flag — base path. Phase 1 file replay writes only
    // <base>.csv; the raw stream is not duplicated (input file is source of
    // truth). Later phases write <base>.raw.txt for live transports.
    app.add_option("--log", opts.logging.log_base,
                   "Log base path: writes <base>.csv (decoded). For live transports "
                   "also writes <base>.raw.txt (raw capture)")
        ->expected(1);
    app.add_option("--adapter-protocol", opts.logging.adapter_protocol,
                   "Adapter protocol: 'raw' (default) or 'elm327'. Default table "
                   "applies when omitted: demo/file/tcp/usb→raw, ble→elm327")
        ->expected(1)
        ->capture_default_str();
    app.add_flag("--stdout-csv", opts.telemetry.stdout_csv,
                 "Emit decoded CSV rows to stdout (same schema as <base>.csv); "
                 "progress output moves to stderr so stdout stays pipeable");
    app.add_option("--start-from", opts.telemetry.start_from_s,
                   "Replay-only: skip rows whose recorded timestamp is before "
                   "this many seconds (mirrors engine-sim-cli --start-from)")
        ->expected(1)
        ->capture_default_str();
    app.add_flag("-k,--interactive", opts.telemetry.interactive_mode,
                 "Keyboard-driven bench mode: read throttle/gear/steering/brake "
                 "from the keyboard (1-9 = 10-90% throttle, 0 = 100%, arrows = "
                 "gear/steering, b = brake, q = quit) and emit CSV rows on stdout "
                 "at --interval Hz. Use with --stdout-csv for a clean pipe.");

    // WiFi provisioning over the device's AT console (AT command set). Two
    // interchangeable transports reach it: USB serial (local,
    // pre-association — no AUTH) and the WiFi TCP console (the AUTH'd console
    // the firmware serves once associated; the same AT commands). The
    // transport is the universal --connect (set above) / --connect-tcp alias.
    // Without --connect, the provisioner auto-detects the first /dev/cu.*
    // match.
    app.add_option("--set-wifi-creds", setWifiArgs,
                   "Provision WiFi credentials over the device console "
                   "(ATSETWIFI). Takes <SSID> <PASS>. Use --connect "
                   "usb:/path, --connect tcp:<ip>[:<port>], or --connect auto "
                   "to pick the device")
        ->expected(2);
    app.add_flag("--clear-wifi-creds", opts.wifi.clear_wifi_creds,
                 "Clear WiFi credentials over the device console "
                 "(ATCLEARWIFI). Use --connect usb:/path, --connect "
                 "tcp:<ip>[:<port>], or --connect auto to pick the device");
    app.add_flag("--reboot", opts.wifi.reboot_esp32,
                 "Reboot the ESP32 over the device console (ATREBOOT). "
                 "Use --connect usb:/path, --connect tcp:<ip>[:<port>], or "
                 "--connect auto to pick the device");
    app.add_flag("--status", opts.wifi.status_requested,
                 "Print a [STATE] snapshot from the device (uptime / wifi / "
                 "ssid / ip / client / disc / led / monitor) by reading its "
                 "next heartbeat line from the USB serial or TCP console. "
                 "Use --connect usb:/path, --connect tcp:<ip>[:<port>], or "
                 "--connect auto to pick the device");

    // Footer for --help: just a one-line pointer to --examples. The
    // OPTIONS list itself is derived from the registrations above, so every
    // registered flag is shown by construction. The EXAMPLES / NOTES /
    // REQUIREMENTS blocks have moved: EXAMPLES is its own --examples flag
    // (and the focus-filtered --help --<option> path); NOTES are folded into
    // the relevant option descriptions; REQUIREMENTS is on --examples.
    app.footer("Run --examples for usage examples. --help --<option> filters examples to a topic.");
}

// True if any of `keys` appears verbatim in argv[1..argc-1].
bool argvContainsAny(int argc, char* argv[],
                     std::initializer_list<std::string_view> keys) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        for (const auto k : keys) {
            if (a == k) return true;
        }
    }
    return false;
}

// Strip leading '-' characters from a flag token to get the bare name.
// "--connect" → "connect", "-h" → "h", "demo" → "demo".
std::string stripLeadingDashes(std::string_view token) {
    const auto pos = token.find_first_not_of('-');
    if (pos == std::string_view::npos) return {};
    return std::string(token.substr(pos));
}

// True when the next argv slot is a value (not a flag) — i.e. the token at
// `i` takes a value that must NOT be treated as a focus token. The value-skip
// is the only place the loop index advances by more than one; hoisting the
// predicate makes that the single mutation point (cpp:S886).
bool takesValue(std::string_view token) {
    return !token.empty() && token.front() != '-';
}

// Populate `focus` from the argv tokens alongside --help / --examples.
// Anything that isn't itself a help flag and starts with '-' is treated as a
// bare topic name; the value that follows a value-taking option is skipped
// (heuristic: skip the next slot if it doesn't itself start with '-'). This
// keeps `--help --connect demo` from treating `demo` as a focus token.
void computeHelpFocus(int argc, char* argv[], std::vector<std::string>& focus) {
    int i = 1;
    while (i < argc) {
        const std::string_view a = argv[i];
        if (a == "--help" || a == "-h" || a == "--examples") {
            ++i;
            continue;
        }
        const std::string bare = stripLeadingDashes(a);
        if (bare.empty()) {
            ++i;
            continue;
        }
        focus.push_back(bare);
        // Skip the value that follows a value-taking option — this is the
        // ONLY place `i` is mutated beyond the stride.
        if (i + 1 < argc && takesValue(argv[i + 1])) {
            i += 2;
        } else {
            ++i;
        }
    }
}

// Trim leading and trailing whitespace from `sv`. Returns empty string for
// an all-whitespace input. Used to normalize CSV option-name segments.
std::string trimmed(std::string_view sv) {
    const auto a = sv.find_first_not_of(" \t");
    if (a == std::string_view::npos) return {};
    const auto b = sv.find_last_not_of(" \t");
    return std::string(sv.substr(a, b - a + 1));
}

// Split a comma-separated token list into its trimmed segments. Empty or
// all-whitespace segments are dropped (e.g. "a, , b" → ["a","b"]). Used to
// split CLI11's ", "-joined option-name aliases into individually matchable
// names.
std::vector<std::string_view> splitCommaSeparated(std::string_view raw) {
    std::vector<std::string_view> segs;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t end = raw.find(',', start);
        const std::size_t segLen =
            (end == std::string::npos) ? raw.size() - start : end - start;
        segs.push_back(raw.substr(start, segLen));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return segs;
}

// Return a vector of the names accepted by `app`, both the canonical
// long form ("--connect") and any short aliases ("-c"). Used to validate
// the focus tokens alongside --examples / --help: a token that starts with
// "--" but isn't on this list is an unknown option and must be rejected,
// while a token without leading dashes is a bare focus name (e.g.
// `--examples connect`).
std::vector<std::string> registeredOptionNames(const CLI::App& app) {
    std::vector<std::string> names;
    for (const CLI::Option* opt : app.get_options()) {
        // `get_name()` may report several aliases separated by ','. Split
        // them so the lookup matches each individually.
        for (const auto& seg : splitCommaSeparated(opt->get_name())) {
            std::string name = trimmed(seg);
            if (!name.empty()) names.push_back(std::move(name));
        }
    }
    return names;
}

// True if `token` is a registered option name on `app`. Used to
// distinguish a focus token (`--connect`, which IS a registered option)
// from an unknown flag (`--foo`, which is NOT). The set is rebuilt on
// each call — the list is small (a few dozen names) and lives only on
// the help/examples short-circuit path, so caching is not worth the
// invalidation hazard.
bool isRegisteredOption(const CLI::App& app, std::string_view token) {
    const std::vector<std::string> names = registeredOptionNames(app);
    return std::any_of(names.begin(), names.end(),
                       [&](const std::string& n) { return n == token; });
}

// When --examples / --help was requested, build a "clean" argv that strips
// the focus tokens out so the underlying CLI11 parse doesn't see them as
// Extras. Returns the new argc and a vector of (mutable) char* aliases
// (whose backing strings live in `clean_storage`). The original argv is
// untouched — computeHelpFocus() still runs over it afterwards.
//
// `unknown_error` is set to a user-facing message if any focus token looks
// like an unknown flag (starts with "--" but isn't on the registered
// options list). The caller reports this as a parse error so the user
// gets the same "unknown option" surface they'd see without --examples.
struct CleanArgv {
    std::vector<std::string> storage;   // owns the strings
    std::vector<char*> ptrs;            // argv-style pointers into storage
};

CleanArgv buildCleanArgv(const CLI::App& app, int argc, char* argv[],
                         bool shortCircuit, std::string& unknown_error) {
    CleanArgv out;
    out.storage.reserve(argc);
    out.ptrs.reserve(argc);
    out.storage.emplace_back(argv[0]);  // program name
    out.ptrs.push_back(out.storage.back().data());
    for (int i = 1; i < argc; ++i) {
        const std::string_view tok(argv[i]);
        if (tok == "--help" || tok == "-h" || tok == "--examples") {
            out.storage.emplace_back(tok);
            out.ptrs.push_back(out.storage.back().data());
            continue;
        }
        if (!shortCircuit) {
            // Not in a help/examples short-circuit — pass through verbatim
            // (we own a copy anyway; this is the safe KISS path).
            out.storage.emplace_back(tok);
            out.ptrs.push_back(out.storage.back().data());
            continue;
        }
        if (const std::string bare = stripLeadingDashes(tok); bare.empty()) {
            out.storage.emplace_back(tok);
            out.ptrs.push_back(out.storage.back().data());
            continue;
        }
        // A token that starts with "--" must be a registered option, OR an
        // unknown option that we should surface as an error. Bare names
        // (no leading dashes) are always treated as focus tokens.
        if (tok.size() >= 2 && tok[0] == '-' && tok[1] == '-'
            && !isRegisteredOption(app, tok)) {
            unknown_error = std::string("Unknown option: ") +
                            std::string(tok);
            return out;
        }
        // Drop the focus token from the clean argv; it lives in the
        // original argv and computeHelpFocus() will pick it up later.
    }
    return out;
}

// Capture the help_text and/or examples_text that parseArgs should surface
// after a short-circuit path. Called AFTER app.parse() returns (successfully
// OR via a swallowed CallForHelp/ParseError). `wantsHelp` and `wantsExamples`
// are pre-scan flags (the parse may not have set examples_requested yet if
// CallForHelp fired first, so we drive the decision from the pre-scan).
void captureEarlyExitTexts(CliOptions& opts, const CLI::App& app,
                           bool wantsHelp, bool wantsExamples) {
    if (wantsExamples) {
        // --examples ALWAYS short-circuits — even if --help wasn't given —
        // and ALSO satisfies the help request so the orchestrator doesn't
        // fall through to telemetry dispatch. The payload is the embedded
        // copy of assets/examples.md (single source of truth; the file is
        // the SoT and the build embeds it via ExamplesContent.cpp).
        //
        // examples_text is a multi-line layout sink (like help_text): it is
        // composed of static `# section:` / `# topic:` markers and body lines.
        // Route it through forLogKeepNewlines() to sever cfamily's taint at
        // ingress (cpp:S5145) while preserving the newline/tab layout. On
        // this static, LF-delimited content it is a NO-OP, so the examples
        // layout is preserved exactly.
        opts.mode.examples_text = forLogKeepNewlines(kExamplesContent);
        opts.mode.help_requested = true;
    }
    if (wantsHelp) {
        opts.mode.help_text = app.help();
        opts.mode.help_requested = true;
    }
}

// Fold --connect-{usb,ble,tcp,auto} and --connect-file sugar onto
// connect_target so every downstream consumer (isUsb(), main.cpp) sees a
// single source of truth. --connect wins if both are given (CLI11 would
// have rejected duplicates, but a user could still supply the shorthand
// AFTER --connect by editing the struct post-parse).
void foldConnectAliases(CliOptions& opts) {
    if (!opts.telemetry.connect_target.empty()) return;

    if (!opts.telemetry.connect_usb.empty()) {
        opts.telemetry.connect_target = "usb:" + opts.telemetry.connect_usb;
    } else if (opts.telemetry.connect_auto) {
        opts.telemetry.connect_target = "auto";
    } else if (!opts.telemetry.connect_tcp.empty()) {
        // tcp:<ip>[:<port>] — fill the prefix; the user-supplied tail
        // already has the colon, so just prepend 'tcp:'.
        opts.telemetry.connect_target = "tcp:" + opts.telemetry.connect_tcp;
    } else if (!opts.telemetry.connect_file.empty()) {
        // file:<path> — file is the long form of the alias.
        opts.telemetry.connect_target = "file:" + opts.telemetry.connect_file;
    }
    // --connect-ble is a marker; the actual BLE address still arrives via
    // --connect. If only the marker is set, the user forgot to supply an
    // address, which the normal connect-required validator catches.
}

// --set-wifi-creds captures two positional values into a vector; fold them
// onto the struct fields. expected(2) guarantees exactly two elements when
// parsing succeeds.
void foldSetWifiCreds(CliOptions& opts, const std::vector<std::string>& setWifiArgs) {
    if (setWifiArgs.size() == 2) {
        opts.wifi.set_wifi_ssid = setWifiArgs[0];
        opts.wifi.set_wifi_pass = setWifiArgs[1];
    }
}

// Universal transport: when a provisioning flag is set, --connect selects
// the provisioning transport, NOT the telemetry transport. main.cpp
// short-circuits to runProvisioning() so telemetry never runs. We move the
// value off telemetry.connect_target and onto wifi.transport, leaving
// telemetry empty so any downstream telemetry dispatchers see "no telemetry
// requested".
void foldProvisioningTransport(CliOptions& opts) {
    if (!opts.wifi.active()) return;
    if (opts.telemetry.connect_target.empty()) {
        // No --connect was supplied: provisioner auto-detects the USB
        // serial port. The empty transport is the signal; the resolver
        // turns it into a concrete /dev/cu.* path at run time.
        opts.wifi.transport = "";
    } else {
        opts.wifi.transport = std::move(opts.telemetry.connect_target);
        opts.telemetry.connect_target.clear();
    }
}

}  // namespace

// Format a --vehicle validation error message. `reason` is the specific
// failure ("label exceeds N characters" / "control characters are not allowed");
// `label` is the offending value (routed through forLog at the sink to sever
// taint). Single source of truth for the two validateVehicleLabel branches.
std::string invalidVehicleLabelMsg(std::string_view reason,
                                    const std::string& label) {
    std::ostringstream oss;
    oss << "Invalid --vehicle '" << forLog(label) << "': " << reason;
    return oss.str();
}

// Boundary validation for a free-form vehicle label (interactive mode and
// decoded-CSV replay). A CR/LF/control byte in this label would already
// corrupt the emitted CSV row today, so rejecting is a correctness fix as much
// as a log-injection (cpp:S5145) remedy: the value flows unsubstituted into the
// CSV DATA sink, which must stay byte-contract-clean. Reject control characters
// (< 0x20 or 0x7F) and over-length labels; return an empty string if OK.
static std::string validateVehicleLabel(const std::string& label) {
    if (constexpr std::size_t kMaxVehicleLabel = 64; label.size() > kMaxVehicleLabel) {
        return invalidVehicleLabelMsg(
            "label exceeds " + std::to_string(kMaxVehicleLabel) + " characters", label);
    }
    for (const unsigned char c : label) {
        if (c < 0x20 || c == 0x7F) {
            return invalidVehicleLabelMsg("control characters are not allowed", label);
        }
    }
    return "";
}

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    CLI::App app{"Vehicle OBD2 Telemetry Display", "vehicle-sim"};
    std::vector<std::string> setWifiArgs;
    registerOptions(app, opts, setWifiArgs);

    // Pre-scan argv for the early-exit flags. A `--help --connect` line
    // would otherwise hit a RequiredError on --connect (no value) before
    // CLI11's CallForHelp check fires; pre-scanning lets us suppress the
    // parse error and capture the help text instead. --examples is a
    // regular flag and never throws CallForHelp, so we must also drive it
    // from the pre-scan.
    const bool wantsHelp = argvContainsAny(argc, argv, {"--help", "-h"});
    const bool wantsExamples = argvContains(argc, argv, "--examples");
    const bool shortCircuit = wantsHelp || wantsExamples;

    // When the user asked for --help or --examples, every argv token that
    // isn't one of those flags is a FOCUS token, not a CLI11 option. We
    // build a "clean" argv with the focus tokens stripped so the parser
    // doesn't throw ExtrasError on `--help --connect` / `--examples
    // --connect` (legitimate filter invocations). Tokens that look like
    // unknown flags (start with `--` but aren't registered) still produce
    // an error so `--examples --foo` doesn't silently exit 0 with an
    // empty EXAMPLES block — the user flagged that as the worst possible
    // UX for a typo.
    std::string unknown_focus_error;
    CleanArgv clean = buildCleanArgv(app, argc, argv, shortCircuit,
                                     unknown_focus_error);
    if (!unknown_focus_error.empty()) {
        opts.error_message = unknown_focus_error;
        return opts;
    }

    try {
        app.parse(static_cast<int>(clean.ptrs.size()), clean.ptrs.data());
    } catch (const CLI::ParseError& e) {
        // CLI::CallForHelp is a ParseError subclass and is swallowed by design
        // here: --help / --examples short-circuit, so we fall through to the
        // capture step below. ExtrasError (unknown options) on the clean argv
        // should be unreachable — the pre-filter caught every focus token that
        // looked like a flag. If we get here it means a real CLI11 option is
        // missing from the registered list, so surface it unconditionally. Any
        // OTHER parse error (RequiredError, ArgumentMismatch, etc.) is swallowed
        // when --help/--examples was requested: the focus-filter scan below
        // still needs the focus tokens, and the orchestrator is going to
        // short-circuit on the captured text.
        const bool unreachableExtras = dynamic_cast<const CLI::ExtrasError*>(&e) != nullptr;
        if (unreachableExtras || !shortCircuit) {
            opts.error_message = e.what();
        }
    }

    captureEarlyExitTexts(opts, app, wantsHelp, wantsExamples);

    if (opts.mode.help_requested || opts.mode.examples_requested) {
        computeHelpFocus(argc, argv, opts.mode.help_focus);
    }

    foldConnectAliases(opts);
    foldSetWifiCreds(opts, setWifiArgs);
    foldProvisioningTransport(opts);

    return opts;
}

void printHelp(std::ostream& out, const std::string& help_text) {
    // The USAGE line, OPTIONS list, and footer are derived from the CLI11
    // registrations via help_text (captured in parseArgs on CallForHelp). This
    // guarantees every registered flag is shown in --help by construction;
    // adding an option in parseArgs can never silently drop it from help.
    //
    // help_text is composed entirely from static option DESCRIPTIONS (string
    // literals registered at compile time) plus the static app name; it never
    // contains any argv-derived value. We route it through
    // cli::forLogKeepNewlines() (which severs cfamily's taint at the sink)
    // while KEEPING the newline/tab the multi-line layout needs — only CR and
    // other control bytes are neutralized. On this static, LF-delimited
    // content it is a NO-OP, so the help layout is preserved exactly.
    out << cli::forLogKeepNewlines(help_text);
}

// Parsed model for the EXAMPLES block. A block is a (topic-names, body-lines)
// pair; the body of a block is everything between its `# topic:` line and the
// next comment line (or end-of-string). A section groups blocks under a heading.
struct ExampleBlock {
    std::vector<std::string> topics;
    std::vector<std::string> lines;
};
struct ExampleSection {
    std::string name;
    std::vector<ExampleBlock> blocks;
};

// Split a comma-separated topic tag into its trimmed, non-empty names.
// "connect, connect-usb , " → ["connect","connect-usb"]. All-whitespace
// segments are dropped (the regression target for the S134 nesting at the
// segLead/segTrail check).
std::vector<std::string> splitTopicNames(std::string_view raw) {
    std::vector<std::string> out;
    for (const auto& seg : splitCommaSeparated(raw)) {
        std::string name = trimmed(seg);
        if (!name.empty()) out.push_back(std::move(name));
    }
    return out;
}

// Hierarchical topic/focus match. `topic` matches `focus` if they are equal,
// if `topic` starts with `<focus>-`, or if `focus` starts with `<topic>-`
// (i.e. `topic` is the prefix-segment of a longer focus). This is the single
// source of truth for the focus filter so the renderer stays a thin composer.
bool topicMatchesFocus(std::string_view topic, std::string_view focus) {
    if (topic == focus) return true;
    // Build the prefix strings explicitly: string_view + literal isn't
    // directly expressible in C++17, so form the prefix and compare via
    // rfind(..., 0) == 0 (the starts_with idiom).
    const std::string focusPrefix(std::string(focus) + "-");
    const std::string topicPrefix(std::string(topic) + "-");
    if (topic.size() >= focusPrefix.size() &&
        topic.compare(0, focusPrefix.size(), focusPrefix) == 0) return true;
    if (focus.size() >= topicPrefix.size() &&
        focus.compare(0, topicPrefix.size(), topicPrefix) == 0) return true;
    return false;
}

// Parse the structured EXAMPLES document into the section/block model.
// Input grammar:
//   # section: <NAME>      — group heading; blocks until the next section line
//   # topic: <name>[, ...] — topic block; body until the next comment line
//   # ...                  — comment (and any other `#` line); stripped
std::vector<ExampleSection> parseExamplesDocument(const std::string& text) {
    std::vector<ExampleSection> sections;

    constexpr std::string_view kTopicTag = "# topic:";
    constexpr std::string_view kSectionTag = "# section:";

    std::istringstream in(text);
    std::string line;
    ExampleBlock* current = nullptr;
    // Anchor: a "default" section so blocks preceding any `# section:` line
    // still render (defensive; the asset always opens with a section).
    sections.emplace_back();
    auto* currentSection = &sections.back();

    auto startsWith = [&](std::string_view s, std::string_view tag) {
        if (s.size() < tag.size()) return false;
        return s.compare(0, tag.size(), tag) == 0;
    };

    while (std::getline(in, line)) {
        // Trim leading whitespace for the marker check.
        const std::string_view lv(line);
        const auto lead = lv.find_first_not_of(" \t");
        if (lead == std::string_view::npos) {
            // Blank line — keep the empty body line if we're inside a
            // block, so the rendered output preserves the inter-block
            // spacing. Strip if no current block.
            if (current) current->lines.push_back(line);
            continue;
        }
        const std::string_view tag = lv.substr(lead);
        if (startsWith(tag, kSectionTag)) {
            // Open a new section. The new section's name is the trimmed tail
            // of the line after the marker; an empty tail is allowed and
            // becomes a blank heading (the renderer suppresses empty heads
            // when nothing matches).
            std::string name(tag.substr(kSectionTag.size()));
            const auto a = name.find_first_not_of(" \t");
            const auto b = name.find_last_not_of(" \t");
            if (a == std::string::npos) name.clear();
            else name = name.substr(a, b - a + 1);
            sections.push_back({std::move(name), {}});
            currentSection = &sections.back();
            current = nullptr;
        } else if (startsWith(tag, kTopicTag)) {
            ExampleBlock b;
            b.topics = splitTopicNames(tag.substr(kTopicTag.size()));
            currentSection->blocks.push_back(std::move(b));
            current = &currentSection->blocks.back();
        } else if (current) {
            // Non-marker line inside a topic block. The only `#`-prefixed
            // lines that survive the marker check above are other markers
            // (which open a new section/block) or plain comments — those
            // close the current block by virtue of NOT being a marker and
            // are dropped here.
            current->lines.push_back(line);
        }
        // `else`: stray comment line outside any block — drop.
    }
    return sections;
}

// Render the parsed EXAMPLES model to `out`, filtered to `focus`. A block
// matches if any of its topic names matches any focus token (hierarchical,
// see topicMatchesFocus). With empty focus every block matches. Output groups
// sections that still have at least one matching block and prints the heading
// once above its matching blocks.
void renderExamples(std::ostream& out, const std::vector<ExampleSection>& sections,
                    const std::vector<std::string>& focus) {
    // Render: for each section with at least one matching block, print the
    // heading (skip if the heading name is empty) followed by the matching
    // blocks, blank-line separated.
    bool firstSection = true;
    for (const auto& s : sections) {
        // Filter the section's blocks up-front so the empty-section case
        // (no matches) collapses naturally.
        std::vector<const ExampleBlock*> kept;
        for (const auto& b : s.blocks) {
            const bool matches =
                focus.empty() ||
                std::any_of(b.topics.begin(), b.topics.end(),
                            [&](const std::string& t) {
                                return std::any_of(focus.begin(), focus.end(),
                                    [&](const std::string& f) {
                                        return topicMatchesFocus(t, f);
                                    });
                            });
            if (matches) kept.push_back(&b);
        }
        if (kept.empty()) continue;
        if (!firstSection) out << "\n";
        firstSection = false;
        if (!s.name.empty()) {
            // `s.name` derives from examples_text (tainted at ingress); route
            // it through forLogKeepNewlines() at the sink to sever cfamily's
            // taint through the parseExamplesDocument() intermediaries (cpp:S5145)
            // while keeping the heading layout intact.
            out << forLogKeepNewlines(s.name) << ":\n";
        }
        bool firstBlock = true;
        for (const auto* b : kept) {
            if (!firstBlock) out << "\n";
            firstBlock = false;
            for (const auto& ln : b->lines)
                // Same taint rationale as the heading above: ln is sourced
                // from examples_text; sanitize at the sink to sever taint.
                out << forLogKeepNewlines(ln) << "\n";
        }
    }
    out << "\n";
}

// printExamples renders the curated EXAMPLES block. The input is structured:
//   # section: <NAME>      — group heading; blocks until the next section line
//   # topic: <name>[, ...] — topic block; body until the next comment line
//   # ...                  — comment (and any other `#` line); stripped
//
// The hierarchical focus filter is unchanged from the previous design: a
// topic matches `focus[i]` if it equals it, starts with `<focus>-`, or is
// the prefix-segment of a longer focus. With an empty focus every block is
// shown. Output groups sections that still have at least one matching block
// and prints the heading once above its matching blocks.
void printExamples(std::ostream& out, const std::string& examples_text,
                   const std::vector<std::string>& focus) {
    out << "EXAMPLES:\n";
    auto sections = parseExamplesDocument(examples_text);
    renderExamples(out, sections, focus);
}

void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service) {
    auto vehicles = service.registry().getRegisteredVehicles();
    for (const auto& id : vehicles) {
        const auto* cfg = service.registry().getConfig(id);
        // Invariant: every id returned by getRegisteredVehicles() is present in
        // the registry's map, so getConfig() cannot return null for it. A null
        // here would be a registry data-structure bug, not a runtime failure.
        assert(cfg != nullptr && "registry yields an id getConfig() cannot find");

        out << "\n" << cfg->vehicleName << " (" << id << "):\n";
        for (const auto& [signalName, fieldName] : cfg->signalMappings) {
            out << "  " << signalName << " -> " << fieldName << "\n";
        }
        out << "  Protocol: " << (cfg->isCANProtocol ? "CAN (DBC)" : "OBD2 (SAE J1979)") << "\n";
    }
    out << "\n";
}

// ===== validateOptions: SRP helpers ============================================
// Each helper has a single responsibility and returns an empty string on
// success or a user-facing error message on failure. validateOptions() is a
// pure composer: it runs each helper in turn and returns the first failure.

namespace {

// True when the mode flags select an info/early-exit command that has no
// telemetry/connect requirements of its own.
bool isInfoMode(const CliOptions& opts) {
    return opts.mode.scan_mode || opts.mode.list_signals || opts.mode.help_requested ||
           opts.mode.discover_mode || opts.mode.led_help ||
           opts.mode.examples_requested;
}

// True when a WiFi-provisioning operation was requested. Provisioning has
// no telemetry/connect/vehicle requirements of its own (the device's AT
// console is reached over USB serial), so validation must short-circuit
// before any telemetry-shaped check fires — `--clear-wifi-creds` alone
// must validate cleanly.
bool isProvisioningMode(const CliOptions& opts) {
    return opts.isProvisioning();
}

// Provisioning has its own transport vocabulary. The device's AT console is
// reachable over USB serial (pre-association) or over the WiFi TCP console
// (the AUTH'd console the firmware serves once associated — the same AT
// command set on both). Accepts empty (auto-detect), 'auto', 'usb:<path>',
// and 'tcp:<host>[:<port>]'. The tcp: grammar is checked with the engine's
// single canonical parser (parseTcpTarget) so the accepted forms match the
// telemetry --connect byte-for-byte — there is no second parser here.
// Anything else is rejected.
std::string validateProvisioningTransport(const CliOptions& opts) {
    if (!opts.isProvisioning()) return "";
    const std::string& t = opts.wifi.transport;
    if (t.empty() || t == "auto" || t.rfind("usb:", 0) == 0) return "";
    if (t.rfind("tcp:", 0) == 0) {
        std::string host;
        int port = 0;
        if (vehicle_sim::pipeline::parseTcpTarget(t, host, port)) return "";
        std::ostringstream oss;
        oss << "Invalid TCP provisioning target '" << forLog(t)
            << "'. Use --connect tcp:<host>[:<port>] with a non-empty host "
               "and a port in [1, 65535] (e.g. tcp:192.168.68.91:3333; the "
               "port defaults to 3333)";
        return oss.str();
    }
    std::ostringstream oss;
    oss << "Provisioning transport '" << forLog(t)
        << "' is not supported. Use --connect auto, --connect usb:<path> "
           "(e.g. usb:/dev/cu.usbserial-110), or --connect "
           "tcp:<host>[:<port>] (e.g. tcp:192.168.68.91:3333)";
    return oss.str();
}

// --adapter-protocol must be a known value (or empty, which uses the default
// table resolved by the pipeline factory).
std::string validateAdapterProtocol(const CliOptions& opts) {
    const auto& p = opts.logging.adapter_protocol;
    if (p.empty() || p == "raw" || p == "elm327" || p == "default") return "";
    std::ostringstream oss;
    oss << "Unknown --adapter-protocol '" << forLog(p)
        << "'. Supported: raw, elm327";
    return oss.str();
}

// --connect is required for telemetry. Interactive mode supplies its own
// synthetic source and is exempt; provisioning also exempts because the
// provisioning track short-circuits at isProvisioningMode() above and
// validateConnectRequired is only called after that gate.
std::string validateConnectRequired(const CliOptions& opts) {
    if (!opts.telemetry.connect_target.empty()) return "";
    if (opts.telemetry.interactive_mode) return "";
    if (opts.isProvisioning()) return "";
    return "--connect is required. Use --connect demo, --connect auto, or --connect <address>";
}

// Interactive mode: --vehicle is a free-form label stamped onto each emitted
// CSV row, so it must pass boundary validation. The label flows UNSUBSTITUTED
// into the CSV DATA sink (cpp:S5145).
std::string validateInteractiveVehicleLabel(const CliOptions& opts) {
    if (!opts.telemetry.interactive_mode) return "";
    return validateVehicleLabel(opts.telemetry.vehicle_type);
}

// Decoded-telemetry CSV replay: --vehicle is a free-form label stamped onto
// each emitted row. Raw CAN replay (file:<raw>) still requires a registered
// vehicle for DBC translation, handled by validateVehicle.
std::string validateDecodedCsvReplayLabel(const CliOptions& opts) {
    if (!opts.isFile()) return "";
    if (!isDecodedTelemetryCsv(opts.telemetry.connect_target.substr(5))) return "";
    return validateVehicleLabel(opts.telemetry.vehicle_type);
}

// --vehicle resolution against the registry: empty → error, 'auto' → must be
// BLE, anything else → must be a registered vehicle id.
std::string validateVehicle(const CliOptions& opts,
                            const domain::VehicleConfigRegistry& registry) {
    const auto& v = opts.telemetry.vehicle_type;
    if (v.empty()) {
        std::ostringstream oss;
        oss << "--vehicle is required. Available: ";
        for (const auto& id : registry.getRegisteredVehicles()) {
            oss << id << " ";
        }
        return oss.str();
    }
    if (v == "auto") {
        if (!opts.isBLE()) {
            return "--vehicle auto requires a BLE connection. Use --connect <address> --vehicle auto";
        }
        return "";
    }
    if (!registry.hasConfig(v)) {
        std::ostringstream oss;
        oss << "Unsupported vehicle type '" << forLog(v) << "'. Available: ";
        for (const auto& id : registry.getRegisteredVehicles()) {
            oss << id << " ";
        }
        return oss.str();
    }
    return "";
}

}  // namespace

std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service) {
    auto& registry = service.registry();

    // Provisioning is its own pre-validated track: any telemetry-shaped errors
    // are irrelevant because main.cpp short-circuits to runProvisioning().
    if (auto err = validateProvisioningTransport(opts); !err.empty()) {
        return err;
    }

    // Info/early-exit modes and provisioning have no telemetry/connect/vehicle
    // requirements of their own and short-circuit before any further checks.
    if (isInfoMode(opts) || isProvisioningMode(opts)) {
        return "";
    }

    if (auto err = validateAdapterProtocol(opts); !err.empty()) {
        return err;
    }
    if (auto err = validateConnectRequired(opts); !err.empty()) {
        return err;
    }
    if (auto err = validateInteractiveVehicleLabel(opts); !err.empty()) {
        return err;
    }
    if (auto err = validateDecodedCsvReplayLabel(opts); !err.empty()) {
        return err;
    }
    if (auto err = validateVehicle(opts, registry); !err.empty()) {
        return err;
    }
    return "";
}

void printLedHelp(std::ostream& out) {
    // Compact one-line-per-pattern diagnostic table, generated from the pattern
    // opcode arrays (single source of truth in firmware/can-bridge/StatusLED.cpp).
    out << firmware::StatusLEDRenderer::generateTable();
}

} // namespace vehicle_sim::cli
