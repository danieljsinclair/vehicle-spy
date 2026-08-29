#include <iostream>
#include <memory>
#include <string_view>
#include <csignal>
#include <cstdio>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/cli/CsvReplayPath.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/cli/ProvisioningRunner.h"
#include "vehicle-sim/cli/BLERunContext.h"
#include "vehicle-sim/cli/ReplayRunContext.h"
#include "vehicle-sim/cli/LiveRunContext.h"
#include "vehicle-sim/cli/CsvReplayRunContext.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/cli/InteractiveRunContext.h"
#include "vehicle-sim/io/FileCsvTelemetrySource.h"
#include "vehicle-sim/util/IClock.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/SignalStopBroker.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/discovery/UDPDiscovery.h"

namespace {

constexpr int BLE_SCAN_TIMEOUT_S = 10;

int runScan(vehicle_sim::BLEManager& bleManager) {
    using namespace vehicle_sim;

    std::cout << "\nScanning for BLE devices (" << BLE_SCAN_TIMEOUT_S << " seconds)...\n";
    std::cout << "Note: Make sure your vehicle is ON and OBD2 adapter is connected.\n\n";

    auto devices = bleManager.scanForDevices(BLE_SCAN_TIMEOUT_S);

    if (devices.empty()) {
        std::cout << "\nNo BLE devices found.\n\n"
                          << "Troubleshooting:\n"
                          << "  1. Ensure your vehicle is powered ON (accessory mode or drive mode)\n"
                          << "  2. Verify OBD2 adapter is connected to vehicle's OBD-II port\n"
                          << "  3. Check adapter has power (some require external power)\n"
                          << "  4. On macOS, grant Bluetooth permissions if prompted\n"
                          << "  5. Try moving closer to vehicle (BLE range ~10m)\n";
        return 1;
    }

    std::cout << "\nFound " << devices.size() << " BLE device(s).\n";
    std::cout << "To connect: vehicle-sim --connect <address> --vehicle <type>\n\n";
    return 0;
}

// Run UDP discovery and list found ESP32s.
int runDiscovery() {
    using namespace vehicle_sim::discovery;
    using namespace vehicle_sim::pipeline;

    std::cout << "Listening for ESP32 discovery broadcasts on UDP port "
              << DISCOVERY_PORT << "...\n";
    std::cout << "Press Ctrl-C to stop.\n\n";

    // Cooperative stop: the shared StopToken is published to the broker and
    // polled by discovery; the signal handler flips it via the async-signal-safe
    // onStopSignal (one atomic load + one atomic store — no cout/endl).
    auto stop = std::make_shared<StopToken>();
    signal_stop_broker::brokerSet(stop.get());
    std::signal(SIGINT, vehicle_sim_onStopSignal);
    std::signal(SIGTERM, vehicle_sim_onStopSignal);
    // RAII scope guard: non-copyable, non-movable — clears the broker on scope exit.
    struct BrokerClear {
        BrokerClear() = default;
        ~BrokerClear() { signal_stop_broker::brokerClear(); }
        BrokerClear(const BrokerClear&) = delete;
        BrokerClear& operator=(const BrokerClear&) = delete;
        BrokerClear(BrokerClear&&) = delete;
        BrokerClear& operator=(BrokerClear&&) = delete;
    };
    BrokerClear clearer;

    UDPDiscovery discovery{stop};

    // Note: Discovery packets are intentionally unsigned (per commit 8a0acde).
    // Signature verification is only used for OTA updates, not discovery.
    // Discovery is the bootstrap that learns device IPs before any secure channel exists.
    std::cout << "Discovery mode: UNSIGNED (accepting all broadcasts)\n\n";

    if (!discovery.start()) {
        std::cerr << "Failed to start UDP discovery listener on port "
                  << DISCOVERY_PORT << "\n";
        return 1;
    }

    // Poll for AUTO_DISCOVERY_TIMEOUT_S, showing results as they come
    auto devices = discovery.poll(std::chrono::seconds(vehicle_sim::cli::AUTO_DISCOVERY_TIMEOUT_S));
    discovery.stop();

    if (devices.empty()) {
        std::cout << "No ESP32 devices discovered.\n\n"
                  << "Troubleshooting:\n"
                  << "  1. Ensure the ESP32 is powered on and connected to WiFi\n"
                  << "  2. Check that the ESP32 firmware includes UDP discovery\n"
                  << "  3. Verify both devices are on the same subnet\n"
                  << "  4. Check firewall rules for UDP port " << DISCOVERY_PORT << "\n";
        return 1;
    }

    std::cout << "Discovered " << devices.size() << " ESP32 device(s):\n\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        std::cout << "  [" << (i + 1) << "] " << d.address << "\n"
                  << "      CAN:  " << d.tcpConnectionString() << "\n"
                  << "      OTA:  tcp:" << d.address << ":" << d.otaPort << "\n";
    }
    std::cout << "\nConnect with: vehicle-sim --connect tcp:<ip>:<port> --vehicle <type>\n"
              << "   or:        vehicle-sim --connect auto --vehicle <type>\n";

    return 0;
}

// Auto-discover an ESP32 and return a connection string the caller can feed
// straight into connect_target. Returns empty string if no device found.
//
// Resolution order (fast path first):
//   1. USB serial — autoDetectSerialPort() globs /dev/cu.{usbserial,SLAB,wchusbserial}*.
//      If a USB device is present we're done instantly (no broadcast needed).
//      Returns "usb:<path>".
//   2. UDP broadcast discovery — poll the network for discovery beacons up to
//      AUTO_DISCOVERY_TIMEOUT_S. Returns "tcp:<ip>:<port>" of the first responder.
//   3. Nothing found — return empty (caller surfaces a clean failure).
std::string autoDiscoverESP32() {
    // 1. USB-first: a connected device resolves over the serial console with no
    //    network dependency and no wait.
    if (const std::string usbPath = vehicle_sim::cli::autoDetectSerialPort(); !usbPath.empty()) {
        return "usb:" + usbPath;
    }

    // 2. Fall back to UDP broadcast discovery.
    using namespace vehicle_sim::discovery;
    UDPDiscovery discovery;

    // Note: Discovery packets are intentionally unsigned (per commit 8a0acde).

    if (!discovery.start()) {
        std::cerr << "Failed to start UDP discovery\n";
        return {};
    }

    auto devices = discovery.poll(std::chrono::seconds(vehicle_sim::cli::AUTO_DISCOVERY_TIMEOUT_S));
    discovery.stop();

    if (devices.empty()) {
        return {};
    }

    // Return the first discovered device's TCP connection string
    return devices.front().tcpConnectionString();
}

// --status: print a [STATE] snapshot from the device. Short-circuits BEFORE
// the generic provisioning dispatch because status_requested also makes
// wifi.active()/isProvisioning() return true, and runProvisioning() returns
// failure ("No provisioning operation") when no real provisioning op is set
// — which is exactly the case for a pure --status invocation.
//
// Resolution order (USB-first, then network):
//   1. USB serial — if the user gave --connect usb:/path, open it; otherwise
//      autoDetectSerialPort() globs the standard /dev/cu.* prefixes. A
//      detected device resolves INSTANTLY (no broadcast, no wait). Open it
//      and read the next [STATE] heartbeat line.
//   2. No USB device — UDP broadcast discovery (AUTO_DISCOVERY_TIMEOUT_S)
//      to learn the device's TCP address, then open a TCP console connection
//      and read the next [STATE] heartbeat line. The firmware serves
//      [STATE] heartbeats over TCP to an authenticated client (the device
//      writes them to its adopted client via TcpServerManager), so the TCP
//      console port authenticates ("AUTH <token>" → "OK") before reading —
//      the same handshake the live TCP transport does. The heartbeat is on a
//      5s cadence, so PROVISION_STATUS_TIMEOUT_S covers one cycle + jitter.
//   3. Nothing found — clean failure (no hanging on a wrong default port).
int runStatusFlow(const vehicle_sim::cli::CliOptions& opts) {
    // 1. USB-first.
    std::string usbPath;
    if (opts.wifi.transport.rfind("usb:", 0) == 0) {
        usbPath = opts.wifi.transport.substr(4);
    } else {
        usbPath = vehicle_sim::cli::autoDetectSerialPort();
    }
    if (!usbPath.empty()) {
        auto port = vehicle_sim::cli::createSerialPort(usbPath);
        if (!port->open()) {
            // Name the port that failed so the operator sees WHICH device
            // path failed — mirrors runProvisioning()'s diagnostic.
            std::cerr << "[provision] Failed to open serial port "
                      << vehicle_sim::cli::forLog(usbPath) << "\n";
            return 1;
        }
        const int rc = vehicle_sim::cli::runStatus(*port, vehicle_sim::cli::PROVISION_STATUS_TIMEOUT_S,
                                                   std::cout, std::cerr);
        // runStatus already returns 1 on timeout / peer-closed with a stderr
        // diagnostic; close the port before returning the rc.
        port->close();
        return rc;
    }

    // 2. No USB device — discover the device's TCP address, then read the
    //    [STATE] heartbeat over a TCP console connection. We learn the
    //    address via UDP broadcast discovery (the device floods beacons) and
    //    parse host/port with the canonical TCP-target parser so the result
    //    is byte-identical to what --connect tcp:<ip>:<port> would resolve.
    std::cout << "No USB serial device found; listening for ESP32 discovery "
                 "beacons on UDP port " << vehicle_sim::discovery::DISCOVERY_PORT << "...\n";
    // Narrow `discovered`'s scope with an if-init-statement (cpp:S6004) —
    // the value is only consulted inside this branch, so don't leak it into
    // the enclosing scope where the clean-failure path could read a stale
    // result.
    if (auto discovered = autoDiscoverESP32(); !discovered.empty()) {
        std::string host;
        int port = 3333;  // firmware console/CAN TCP port; parseTcpTarget
                          // applies this default when no port is in the target.
        // discovered is "tcp:<ip>:<port>" — parse with the canonical parser.
        // If parsing fails (defensive: discovery always emits a valid
        // tcp: string), fall back to reporting the address as proof of life.
        if (!vehicle_sim::pipeline::parseTcpTarget(discovered, host, port)) {
            std::cout << "ESP32 reachable at " << discovered
                      << " (could not parse TCP address — use "
                         "--connect tcp:<ip>:<port> --status, or "
                         "--discover to scan)\n";
            return 0;
        }

        std::cout << "ESP32 reachable at " << discovered
                  << "; reading [STATE] over TCP console...\n";
        auto consolePort = vehicle_sim::cli::createTcpConsolePort(host, port);
        if (!consolePort->open()) {
            // open() failed at connect or AUTH — the device is on the
            // network (discovery saw it) but the console session didn't
            // come up. Surface both facts so the operator can distinguish
            // "device alive, console unreachable" from "no device".
            std::cerr << "[provision] Failed to open TCP console to "
                      << vehicle_sim::cli::forLog(host) << ":" << port
                      << " (device discovered at " << discovered
                      << " — AUTH/connect failed; the device may be mid-reboot "
                         "or the auth token may differ)\n";
            return 1;
        }
        const int rc = vehicle_sim::cli::runStatus(*consolePort, vehicle_sim::cli::PROVISION_STATUS_TIMEOUT_S,
                                                   std::cout, std::cerr);
        consolePort->close();
        return rc;
    }

    // 3. Clean failure.
    std::cerr << "[provision] No ESP32 found on USB serial or the network. "
                 "Connect the device over USB for [STATE] reads, or use "
                 "--discover to scan the network manually.\n";
    return 1;
}

// Handle --connect auto: discover ESP32 and assign to connect_target.
// autoDiscoverESP32() tries USB serial first (instant), then falls back to
// UDP broadcast discovery (AUTO_DISCOVERY_TIMEOUT_S). The result is either
// "usb:<path>" or "tcp:<ip>:<port>" — both handled by runTransport() below.
int autoDiscoverAndAssign(vehicle_sim::cli::CliOptions& opts) {
    using namespace vehicle_sim::cli;
    std::cout << "Auto-discovering ESP32...\n";
    std::string target = autoDiscoverESP32();
    if (target.empty()) {
        std::cerr << "No ESP32 found on the network. Use --discover to scan manually.\n";
        return 1;
    }
    std::cout << "Found ESP32 at " << target << "\n";
    opts.telemetry.connect_target = target;
    return 0;
}

// File replay dispatch: decoded-telemetry CSV (CSV replay mode) routes to
// CsvReplayRunContext; raw CAN captures keep the DBC-translation replay path.
int runFileReplay(const vehicle_sim::cli::CliOptions& opts,
                  vehicle_sim::domain::DBCTranslationService& translationService) {
    using namespace vehicle_sim::cli;
    std::string path = opts.telemetry.connect_target.substr(5);

    // Decoded-telemetry CSV (CSV replay mode) routes to CsvReplayRunContext,
    // which replays the recorded rows as if they were live CAN — feeding the
    // same --stdout-csv schema used for latency testing. Raw CAN captures
    // keep the existing DBC-translation replay path.
    if (isDecodedTelemetryCsv(path)) {
        auto source = std::make_unique<vehicle_sim::io::FileCsvTelemetrySource>(path);
        vehicle_sim::util::SystemClock systemClock;
        return CsvReplayRunContext::run(
            std::move(source), opts.telemetry.vehicle_type,
            opts.telemetry.update_interval_ms, std::cout, systemClock,
            opts.telemetry.stdout_csv);
    }

    // Raw CAN replay through the canonical seam: FileTransport →
    // CaptureNormaliser → DBCTranslationService → DecodedCsvSink. The input
    // file is the raw source of truth, so we write ONLY <base>.csv.
    std::string logBase = opts.logging.log_base;
    return ReplayRunContext::run(path, opts.telemetry.vehicle_type,
                                  logBase, translationService,
                                  opts.telemetry.stdout_csv,
                                  opts.telemetry.start_from_s);
}

// Live transport dispatch: tcp/demo/usb → LiveRunContext, ble → BLERunContext.
int runTransport(const vehicle_sim::cli::CliOptions& opts,
                 vehicle_sim::domain::DBCTranslationService& translationService) {
    using namespace vehicle_sim::cli;
    if (opts.isTcp() || opts.isDemo() || opts.isUsb()) {
        // Live transports (demo/tcp/usb) through the canonical seam:
        // (Demo|TCP|USB)Transport → Normaliser → DBCTranslationService →
        // RawLogSink + DecodedCsvSink. The resolved --log base drives BOTH
        // sinks for live (the raw stream is the source of truth). The adapter
        // protocol default table + explicit override resolve here.
        std::string logBase = opts.logging.log_base;
        std::string protocol = vehicle_sim::pipeline::resolveAdapterProtocol(
            opts.telemetry.connect_target, opts.logging.adapter_protocol);
        return LiveRunContext::run(opts.telemetry.connect_target, opts.telemetry.vehicle_type,
                                    protocol, logBase, translationService,
                                    opts.telemetry.stdout_csv);
    }

    if (opts.isBLE()) {
        return BLERunContext::run(opts.telemetry.connect_target, opts.telemetry.vehicle_type,
                                  translationService);
    }

    // No recognized connect target — validation should have caught this, but
    // fail closed rather than falling through to a default.
    std::cerr << "No telemetry source for connect target: " << forLog(opts.telemetry.connect_target) << "\n";
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace vehicle_sim;

    // Parse BEFORE the banner: --stdout-csv decides which stream the banner
    // belongs on, and a banner already written to stdout cannot be recalled.
    auto opts = cli::parseArgs(argc, argv);

    // When --stdout-csv is active, make the C stdio layer line-buffered so each
    // CSV row (newline-terminated) is flushed through the pipe immediately.
    // std::cout carries both a C++ iostream buffer and a C stdio FILE* buffer;
    // out_.flush() (in CsvStdoutSink) only flushes the iostream layer. The C
    // layer remains block-buffered when piped, causing ~0.5s latency bursts.
    // setvbuf must be called before any I/O on the stream.
    if (opts.telemetry.stdout_csv) {
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    }

    cli::printBanner(opts.telemetry.stdout_csv ? std::cerr : std::cout);

    domain::DBCTranslationService translationService;
    domain::DefaultVehicleConfigs::registerAll(translationService.registry());

    if (!opts.error_message.empty()) {
        std::cerr << opts.error_message << "\n";
        return 1;
    }

    if (auto validationError = cli::validateOptions(opts, translationService); !validationError.empty()) {
        std::cerr << validationError << "\n";
        return 1;
    }

    if (cli::handleEarlyExit(opts, translationService)) {
        return 0;
    }

    if (opts.mode.scan_mode) {
        auto bleManager = std::make_unique<BLEManager>();
        return runScan(*bleManager);
    }

    if (opts.mode.discover_mode) {
        return runDiscovery();
    }

    if (opts.wifi.status_requested) {
        return runStatusFlow(opts);
    }

    // WiFi provisioning over USB serial (AT command set). Local, pre-association
    // — no AUTH, no vehicle registry, no telemetry. Short-circuits before any
    // connect-target handling.
    if (opts.isProvisioning()) {
        return cli::runProvisioning(opts.wifi, std::cout, std::cerr);
    }

    // Interactive bench mode: keyboard-driven CSV emission, no vehicle registry
    // or live transport required. Deterministic pace via a real SystemClock.
    if (opts.telemetry.interactive_mode) {
        vehicle_sim::util::SystemClock systemClock;
        return cli::InteractiveRunContext::run(
            opts.telemetry.vehicle_type.empty() ? std::string{"tesla"} : opts.telemetry.vehicle_type,
            opts.telemetry.update_interval_ms,
            std::cout,
            systemClock);
    }

    // Handle --connect auto: discover ESP32 and assign to connect_target,
    // then fall through to the transport dispatch.
    if (opts.isAuto()) {
        if (int rc = autoDiscoverAndAssign(opts); rc != 0) return rc;
    }

    if (opts.isFile()) {
        return runFileReplay(opts, translationService);
    }

    return runTransport(opts, translationService);
}
