#include <iostream>
#include <memory>
#include <string_view>
#include <csignal>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/cli/BLERunContext.h"
#include "vehicle-sim/cli/ReplayRunContext.h"
#include "vehicle-sim/cli/LiveRunContext.h"
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
    std::signal(SIGINT, signal_stop_broker::onStopSignal);
    std::signal(SIGTERM, signal_stop_broker::onStopSignal);
    struct BrokerClear { ~BrokerClear(){ signal_stop_broker::brokerClear(); } } clearer;

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

    // Poll for 30 seconds, showing results as they come
    auto devices = discovery.poll(std::chrono::seconds(30));
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

// Auto-discover an ESP32 and return its TCP connection string.
// Returns empty string if no device found.
std::string autoDiscoverESP32(std::chrono::seconds timeout = std::chrono::seconds(10)) {
    using namespace vehicle_sim::discovery;

    UDPDiscovery discovery;

    // Note: Discovery packets are intentionally unsigned (per commit 8a0acde).

    if (!discovery.start()) {
        std::cerr << "Failed to start UDP discovery\n";
        return {};
    }

    auto devices = discovery.poll(timeout);
    discovery.stop();

    if (devices.empty()) {
        return {};
    }

    // Return the first discovered device's TCP connection string
    return devices.front().tcpConnectionString();
}

// Derive the canonical --log <base> from whichever logging flag the caller
// used. --log is already a base; --log-csv <file> contributes a base by
// stripping a trailing .csv; --log-raw <file> contributes a base by stripping
// a trailing .raw/.raw.txt. The first non-empty source wins so an explicit
// --log is never overridden by a deprecated alias.
std::string resolveLogBase(const vehicle_sim::cli::CliOptions& opts) {
    using namespace vehicle_sim::cli;
    if (!opts.log_base.empty()) {
        return opts.log_base;
    }
    auto stripSuffix = [](std::string s, std::string_view suffix) {
        if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
            s.erase(s.size() - suffix.size());
        }
        return s;
    };
    if (!opts.log_csv.empty()) {
        return stripSuffix(opts.log_csv, ".csv");
    }
    if (!opts.log_raw.empty()) {
        return stripSuffix(stripSuffix(opts.log_raw, ".txt"), ".raw");
    }
    return {};
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace vehicle_sim;

    cli::printBanner();

    domain::DBCTranslationService translationService;
    domain::DefaultVehicleConfigs::registerAll(translationService.registry());

    auto opts = cli::parseArgs(argc, argv);
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

    if (opts.scan_mode) {
        auto bleManager = std::make_unique<BLEManager>();
        return runScan(*bleManager);
    }

    if (opts.discover_mode) {
        return runDiscovery();
    }

    // Handle --connect auto: discover ESP32 and connect
    if (opts.isAuto()) {
        std::cout << "Auto-discovering ESP32...\n";
        std::string target = autoDiscoverESP32();
        if (target.empty()) {
            std::cerr << "No ESP32 found on the network. Use --discover to scan manually.\n";
            return 1;
        }
        std::cout << "Found ESP32 at " << target << "\n";
        opts.connect_target = target;
        // Fall through to the TCP handling below
    }

    if (opts.isFile()) {
        // File replay through the canonical seam: FileTransport →
        // CaptureNormaliser → DBCTranslationService → DecodedCsvSink. The input
        // file is the raw source of truth, so we write ONLY <base>.csv.
        std::string path = opts.connect_target.substr(5);
        std::string logBase = resolveLogBase(opts);
        return cli::ReplayRunContext::run(path, opts.vehicle_type,
                                          logBase, translationService);
    }

    if (opts.isTcp() || opts.isDemo() || opts.isUsb()) {
        // Live transports (demo/tcp/usb) through the canonical seam:
        // (Demo|TCP|USB)Transport → Normaliser → DBCTranslationService →
        // RawLogSink + DecodedCsvSink. The resolved --log base drives BOTH
        // sinks for live (the raw stream is the source of truth). The adapter
        // protocol default table + explicit override resolve here.
        std::string logBase = resolveLogBase(opts);
        std::string protocol = vehicle_sim::pipeline::resolveAdapterProtocol(
            opts.connect_target, opts.adapter_protocol);
        return cli::LiveRunContext::run(opts.connect_target, opts.vehicle_type,
                                        protocol, logBase, translationService);
    }

    if (opts.isBLE()) {
        return cli::BLERunContext::run(opts.connect_target, opts.vehicle_type,
                                      translationService);
    }

    // No recognized connect target — validation should have caught this, but
    // fail closed rather than falling through to a default.
    std::cerr << "No telemetry source for connect target: " << opts.connect_target << "\n";
    return 1;
}
