#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "StatusLEDRenderer.h"

#include <CLI/CLI.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <fstream>

// Shared time parser from engine-sim-bridge (DRY).
#include "common/TimeParser.h"

namespace vehicle_sim::cli {

// Boundary validation for a free-form vehicle label (interactive mode and
// decoded-CSV replay). A CR/LF/control byte in this label would already
// corrupt the emitted CSV row today, so rejecting is a correctness fix as much
// as a log-injection (cpp:S5145) remedy: the value flows unsubstituted into the
// CSV DATA sink, which must stay byte-contract-clean. Reject control characters
// (< 0x20 or 0x7F) and over-length labels; return an empty string if OK.
static std::string validateVehicleLabel(const std::string& label) {
    if (constexpr std::size_t kMaxVehicleLabel = 64; label.size() > kMaxVehicleLabel) {
        std::ostringstream oss;
        oss << "Invalid --vehicle '" << forLog(label) << "': label exceeds "
            << kMaxVehicleLabel << " characters";
        return oss.str();
    }
    for (const unsigned char c : label) {
        if (c < 0x20 || c == 0x7F) {
            std::ostringstream oss;
            oss << "Invalid --vehicle '" << forLog(label)
                << "': control characters are not allowed";
            return oss.str();
        }
    }
    return "";
}

// A decoded-telemetry CSV (for CSV replay mode) is distinguished from a raw
// CAN capture by its header: the decoded schema leads with "timestamp_ms".
// Raw CAN captures never do. Used to relax validation for bench replay.
static bool isDecodedTelemetryCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string header;
    if (!std::getline(in, header)) return false;
    return header.find("timestamp_ms") != std::string::npos;
}

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    CLI::App app{"Vehicle OBD2 Telemetry Display", "vehicle-sim"};
    app.allow_extras(false);

    app.add_flag("-s,--scan", opts.mode.scan_mode, "Scan for BLE OBD2 adapters");
    app.add_flag("-l,--list", opts.mode.list_signals, "List supported signals for each vehicle");
    app.add_flag("--discover", opts.mode.discover_mode, "Discover ESP32 devices on the network via UDP broadcast");
    app.add_flag("--led-help", opts.mode.led_help, "Show StatusLED pattern reference guide");

    // --connect is the universal transport selector. It applies to BOTH
    // telemetry (--connect demo / --connect tcp:... / --connect auto) AND
    // provisioning (--connect auto / --connect usb:/dev/cu...). parseArgs()
    // routes the value to the correct consumer based on which flags are set
    // (see the post-parse folding below).
    //
    // --connect-{usb,ble,tcp,auto} are sugar that fill in the transport
    // prefix the user would otherwise have to type as 'usb:'. The remaining
    // --connect forms (demo / file:<path> / BLE address) keep --connect.
    app.add_option("-c,--connect", opts.telemetry.connect_target,
                   "Connect target (telemetry OR provisioning): 'demo', "
                   "'file:<path>', 'tcp:<ip>:<port>', 'usb:<path>', 'auto', "
                   "or BLE adapter address. For provisioning, 'auto' and "
                   "'usb:<path>' are the only meaningful values")
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
                   "frames from an ESP32 CAN-bridge over WiFi (default port 3333)")
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
        ->check(CLI::Range(0, 60000))
        ->each([&opts](std::string) {
            opts.telemetry.update_interval_explicit = true;
        });
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
    // Deprecated aliases — mapped onto --log semantics in main.cpp.
    app.add_option("--log-csv", opts.logging.log_csv,
                   "(deprecated, use --log <base>) Log decoded CSV telemetry to file")
        ->expected(1);
    app.add_option("--log-raw", opts.logging.log_raw,
                   "(deprecated, use --log <base>) Log raw hex/TWAI data to file")
        ->expected(1);
    app.add_flag("--stdout-csv", opts.telemetry.stdout_csv,
                 "Emit decoded CSV rows to stdout (same schema as <base>.csv); "
                 "progress output moves to stderr so stdout stays pipeable");
    std::string startFromRaw;
    app.add_option("--start-from", startFromRaw,
                   "Replay-only: skip rows whose recorded timestamp is before "
                   "this time (seconds, mm:ss, or hh:mm:ss; mirrors engine-sim-cli --start-from)")
        ->expected(1)
        ->capture_default_str();
    app.add_flag("-k,--interactive", opts.telemetry.interactive_mode,
                 "Keyboard-driven bench mode: read throttle/gear/steering/brake "
                 "from the keyboard (1-9 = 10-90% throttle, 0 = 100%, arrows = "
                 "gear/steering, b = brake, q = quit) and emit CSV rows on stdout "
                 "at --interval Hz. Use with --stdout-csv for a clean pipe.");

    // WiFi provisioning over USB serial (AT command set). Local, pre-association
    // — no AUTH. --set-wifi-creds takes exactly two positional values (SSID,
    // PASS) after the flag; CLI11's expected(2) captures them into a vector.
    //
    // The transport for provisioning is the universal --connect (set above):
    // --connect auto / --connect usb:/path picks the USB serial port. Without
    // --connect, the provisioner auto-detects the first /dev/cu.* match.
    std::vector<std::string> setWifiArgs;
    app.add_option("--set-wifi-creds", setWifiArgs,
                   "Provision WiFi credentials over USB serial (ATSETWIFI). "
                   "Takes <SSID> <PASS>. Use --connect usb:/path or "
                   "--connect auto to pick the device")
        ->expected(2);
    app.add_flag("--clear-wifi-creds", opts.wifi.clear_wifi_creds,
                 "Clear WiFi credentials over USB serial (ATCLEARWIFI). "
                 "Use --connect usb:/path or --connect auto to pick the device");
    app.add_flag("--reboot", opts.wifi.reboot_esp32,
                 "Reboot the ESP32 over USB serial (ATREBOOT). "
                 "Use --connect usb:/path or --connect auto to pick the device");
    app.add_flag("--status", opts.wifi.status_requested,
                 "Print a [STATE] snapshot from the device (uptime / wifi / "
                 "ssid / ip / client / disc / led / monitor) by reading its "
                 "next heartbeat line from the USB serial console. "
                 "Use --connect usb:/path or --connect auto to pick the device");

    // Static, non-option help text (EXAMPLES / NOTES / REQUIREMENTS). Lifted
    // verbatim from the old hand-rendered printHelp so it is appended after
    // CLI11's auto-generated OPTIONS list. Because the OPTIONS themselves are
    // derived from the registrations above, every registered flag is shown in
    // --help by construction — adding an option here can never silently drop it
    // from help again.
    app.footer(R"(EXAMPLES:
  vehicle-sim --connect demo --vehicle tesla
  vehicle-sim --discover
  vehicle-sim --connect auto --vehicle tesla
  vehicle-sim --connect file:capture.csv --vehicle tesla --log-csv decoded.csv
  vehicle-sim --connect-file capture.csv --vehicle tesla --stdout-csv | head -20
  vehicle-sim --interactive --stdout-csv --vehicle tesla --interval 20
  vehicle-sim --connect tcp:192.168.4.1:3333 --vehicle tesla --log-raw x.raw --log-csv x.csv
  vehicle-sim --connect usb:/dev/cu.usbserial-110 --vehicle tesla --log captures/SecondDrive
  vehicle-sim --connect tcp:192.168.4.1 --vehicle tesla
  vehicle-sim --connect file:capture.csv --vehicle tesla --stdout-csv | head -20
  vehicle-sim --connect <addr> --vehicle tesla
  vehicle-sim --connect <addr> --vehicle auto
  vehicle-sim --scan
  vehicle-sim --list
  vehicle-sim --set-wifi-creds MyNet s3cr3tpass --connect auto
  vehicle-sim --set-wifi-creds MyNet s3cr3tpass --connect usb:/dev/cu.usbserial-110
  vehicle-sim --clear-wifi-creds --connect usb:/dev/cu.usbserial-110
  vehicle-sim --reboot --connect auto
  vehicle-sim --status --connect auto

NOTES:
  --connect and --vehicle are required for telemetry
  'auto' discovers an ESP32 on the UDP discovery port (3335) and connects automatically
  file:<path> replays a captured raw CAN CSV
  tcp:<ip>:<port> streams live CAN frames from an ESP32 CAN-bridge over WiFi
    (port defaults to 3333 when omitted; e.g. tcp:192.168.4.1)
  usb:<path> streams live CAN frames from an ESP32 CAN-bridge over USB serial
    (for example /dev/cu.usbserial-110 at 115200 8N1)
  --connect auto / --connect usb:<path> is the universal transport selector:
    for telemetry it picks a CAN source; for provisioning it picks the USB
    serial port. Without --connect, provisioning auto-detects the device.
  tesla and audi_mlb_evo use CAN monitor mode (DBC decoding)
  generic uses standard OBD2 PID polling
  CAN monitor mode is read-only (ATCSM1: no ACK bits on bus)

REQUIREMENTS:
  For real data: Connect a BLE OBD2 adapter to your vehicle's OBD-II port,
  connect over WiFi to an ESP32 CAN-bridge (tcp:<ip>:3333), or connect
  directly by USB serial (usb:/dev/cu.usbserial-110).)");

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        // Capture the CLI11-derived help (auto OPTIONS list + footer) so it can
        // be surfaced verbatim by printHelp. app is still alive in this scope.
        opts.mode.help_requested = true;
        opts.mode.help_text = app.help();
    } catch (const CLI::ParseError& e) {
        opts.error_message = e.what();
    }

    // --connect-{usb,ble,tcp,auto} and --connect-file are sugar that fills in
    // the transport prefix the user would otherwise have to type as e.g. 'usb:'.
    // Fold them onto connect_target so every downstream consumer (isUsb(),
    // main.cpp) sees a single source of truth. --connect wins if both are
    // given (CLI11 would have rejected duplicates, but a user could still
    // supply the shorthand AFTER --connect by editing the struct post-parse).
    if (opts.telemetry.connect_target.empty()) {
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
        } else if (opts.telemetry.connect_ble) {
            // No-op: --connect-ble is a marker; the actual BLE address still
            // arrives via --connect. If only the marker is set, the user
            // forgot to supply an address, which the normal connect-required
            // validator catches.
        }
    }

    // Universal transport: when a provisioning flag is set, --connect
    // selects the provisioning transport, NOT the telemetry transport.
    // main.cpp short-circuits to runProvisioning() so telemetry never runs.
    // We move the value off telemetry.connect_target and onto
    // wifi.transport, leaving telemetry empty so any downstream
    // telemetry dispatchers see "no telemetry requested".
    if (opts.wifi.active()) {
        // Capture BEFORE the move below empties connect_target.
        std::string resolved = opts.telemetry.connect_target;
        if (resolved.empty()) {
            // No --connect was supplied: provisioner auto-detects the USB
            // serial port. The empty transport is the signal; the
            // resolver turns it into a concrete /dev/cu.* path at run time.
            opts.wifi.transport = "";
        } else {
            opts.wifi.transport = std::move(resolved);
            opts.telemetry.connect_target = "";
        }
    }

    // `--set-wifi-creds <SSID> <PASS>` captures two positional values into a
    // vector; fold them onto the struct fields. expected(2) guarantees exactly
    // two elements when parsing succeeds.
    if (setWifiArgs.size() == 2) {
        opts.wifi.set_wifi_ssid = setWifiArgs[0];
        opts.wifi.set_wifi_pass = setWifiArgs[1];
    }

    // Smart timecode parser (DRY with bridge) for --start-from. Applied only
    // when --connect file: (replay mode). The skip stacks with engine-sim-cli's
    // --start-from (both can apply) — expected convenient behavior.
    if (!startFromRaw.empty()) {
        opts.telemetry.start_from_s = engine_sim_bridge::parseTimecodeToSeconds(startFromRaw);
        if (opts.telemetry.start_from_s < 0.0) {
            opts.error_message = "Invalid --start-from time: " + startFromRaw +
                " (expected seconds, mm:ss, or hh:mm:ss)";
        }
    }

    return opts;
}

void printHelp(std::ostream& out, const domain::DBCTranslationService& service,
                const std::string& help_text) {
    auto& registry = service.registry();

    // The USAGE line, OPTIONS list, and footer are derived from the CLI11
    // registrations via help_text (captured in parseArgs on CallForHelp). This
    // guarantees every registered flag — including --set-wifi-creds /
    // --clear-wifi-creds / --reboot / --status / --connect / --connect-{usb,ble,tcp,auto} —
    // is shown in --help by construction; adding an option in parseArgs can
    // never silently drop it from help again.
    //
    // help_text is composed entirely from static option DESCRIPTIONS (string
    // literals registered at compile time in parseArgs) plus the static app name
    // ("vehicle-sim"); it never contains any argv-derived value. It MUST be
    // help_text is NOT external taint in practice: it is composed entirely from
    // static option DESCRIPTIONS (compile-time string literals) plus the static
    // app name ("vehicle-sim"); it never contains any argv-derived value. Sonar
    // flags app.help() as a taint false-positive because the value rides on the
    // parse(argc, argv) call. We route it through cli::forLogKeepNewlines(), which
    // rebuilds the string (severing cfamily's taint at the sink) while KEEPING the
    // newline/tab the multi-line layout needs — only CR and other control bytes are
    // neutralized. On this static, LF-delimited content it is a NO-OP, so the help
    // layout is preserved exactly. (Plain forLog would mangle it; this variant does
    // not.)
    out << cli::forLogKeepNewlines(help_text);

    if (auto vehicles = registry.getRegisteredVehicles(); !vehicles.empty()) {
        out << "SUPPORTED VEHICLES:\n";
        for (const auto& id : vehicles) {
            const auto* cfg = registry.getConfig(id);
            if (cfg) {
                out << "  " << id << "  (" << cfg->vehicleName << ")\n";
            }
        }
        out << "\n";
    }
}

void printSupportedSignals(std::ostream& out, const domain::DBCTranslationService& service) {
    auto vehicles = service.registry().getRegisteredVehicles();
    for (const auto& id : vehicles) {
        const auto* cfg = service.registry().getConfig(id);
        if (!cfg) continue;

        out << "\n" << cfg->vehicleName << " (" << id << "):\n";
        for (const auto& [signalName, fieldName] : cfg->signalMappings) {
            out << "  " << signalName << " -> " << fieldName << "\n";
        }
        out << "  Protocol: " << (cfg->isCANProtocol ? "CAN (DBC)" : "OBD2 (SAE J1979)") << "\n";
    }
    out << "\n";
}

std::string validateOptions(const CliOptions& opts, const domain::DBCTranslationService& service) {
    auto& registry = service.registry();

    // Provisioning has its own transport vocabulary — the device's AT console
    // is only reachable over USB serial, so we reject anything other than
    // 'auto' / 'usb:<path>' / empty (auto-detect) here. Then the rest of the
    // validation is short-circuited.
    if (opts.isProvisioning()) {
        const std::string& t = opts.wifi.transport;
        const bool valid = t.empty() || t == "auto" ||
                           t.rfind("usb:", 0) == 0;
        if (!valid) {
            std::ostringstream oss;
            oss << "Provisioning transport '" << forLog(t)
                << "' is not supported. Use --connect auto or "
                   "--connect usb:<path> (e.g. usb:/dev/cu.usbserial-110)";
            return oss.str();
        }
        return "";
    }

    // Skip validation for scan, list, help, discover, led-help (they have no
    // telemetry/connect requirements of their own).
    if (opts.mode.scan_mode || opts.mode.list_signals || opts.mode.help_requested ||
        opts.mode.discover_mode || opts.mode.led_help) {
        return "";
    }

    // --adapter-protocol must be a known value.
    if (!opts.logging.adapter_protocol.empty() &&
        opts.logging.adapter_protocol != "raw" &&
        opts.logging.adapter_protocol != "elm327" &&
        opts.logging.adapter_protocol != "default") {
        std::ostringstream oss;
        oss << "Unknown --adapter-protocol '" << forLog(opts.logging.adapter_protocol)
            << "'. Supported: raw, elm327";
        return oss.str();
    }

    // --connect is required for telemetry (interactive mode supplies its own
    // synthetic source, so it is exempt).
    if (opts.telemetry.connect_target.empty() && !opts.telemetry.interactive_mode) {
        return "--connect is required. Use --connect demo, --connect auto, or --connect <address>";
    }

    // Interactive mode is self-contained: no vehicle registry lookup needed.
    // The --vehicle label is free-form (stamped onto each emitted CSV row), so
    // it must pass boundary validation before reaching the CSV DATA sink.
    if (opts.telemetry.interactive_mode) {
        if (auto err = validateVehicleLabel(opts.telemetry.vehicle_type); !err.empty()) {
            return err;
        }
        return "";
    }

    // CSV replay of a decoded-telemetry file is bench testing: --vehicle is
    // only a label stamped onto each emitted row, so it need not be a real
    // registered vehicle. It is still free-form and flows unsubstituted into the
    // CSV sink, so it must pass boundary validation. Raw CAN replay
    // (file:<raw>) still requires a valid vehicle for DBC translation, handled
    // below.
    if (opts.isFile()) {
        std::string path = opts.telemetry.connect_target.substr(5);
        if (isDecodedTelemetryCsv(path)) {
            if (auto err = validateVehicleLabel(opts.telemetry.vehicle_type); !err.empty()) {
                return err;
            }
            return "";
        }
    }

    // --vehicle is required
    if (opts.telemetry.vehicle_type.empty()) {
        std::ostringstream oss;
        oss << "--vehicle is required. Available: ";
        auto vehicles = registry.getRegisteredVehicles();
        for (const auto& v : vehicles) {
            oss << v << " ";
        }
        return oss.str();
    }

    // "auto" is valid — resolved at runtime via UDP discovery
    if (opts.telemetry.vehicle_type == "auto") {
        if (!opts.isBLE()) {
            return "--vehicle auto requires a BLE connection. Use --connect <address> --vehicle auto";
        }
        return "";
    }

    // Validate vehicle type against registry
    if (!registry.hasConfig(opts.telemetry.vehicle_type)) {
        std::ostringstream oss;
        oss << "Unsupported vehicle type '" << forLog(opts.telemetry.vehicle_type) << "'. Available: ";
        auto vehicles = registry.getRegisteredVehicles();
        for (const auto& v : vehicles) {
            oss << v << " ";
        }
        return oss.str();
    }

    return "";
}

void printLedHelp(std::ostream& out) {
    // Compact one-line-per-pattern diagnostic table, generated from the pattern
    // opcode arrays (single source of truth in firmware/can-bridge/StatusLED.cpp).
    out << firmware::StatusLEDRenderer::generateTable();
}

} // namespace vehicle_sim::cli
