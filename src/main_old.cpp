#include <iostream>
#include <iomanip>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/VehicleSim.h"
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/presentation/VehicleSignalFormatter.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/boundary/OBD2Protocol.h"

namespace {
    std::atomic<bool> g_running(true);

    // Sleep between spin-loop iterations.
    // 10ms prevents busy-wait CPU burn while keeping response latency low.
    constexpr int SPIN_SLEEP_MS = 10;

    // BLE scan duration. 10s is enough to discover nearby OBD2 adapters.
    constexpr int BLE_SCAN_TIMEOUT_S = 10;

    // Wait after BLE connect for GATT service enumeration on ELM327.
    constexpr int SERVICE_DISCOVERY_WAIT_S = 2;

    // Wait after ELM327 init commands for AT processing.
    constexpr int ELM327_INIT_WAIT_S = 1;

    // Connection health check interval.
    constexpr int BLE_HEALTH_CHECK_MS = 500;

    // Seconds without data before warning.
    constexpr int NO_DATA_WARNING_S = 5;

    // Repeated warnings before "Drive mode" hint (~15s total).
    constexpr int DRIVE_MODE_HINT_COUNT = 3;

    // Minimum raw data length to log as hex.
    constexpr std::size_t RAW_DATA_LOG_MIN_LENGTH = 3;

    void signalHandler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
        g_running = false;
    }

    int runSimulation(
        const std::string& vehicleType,
        const vehicle_sim::domain::VehicleConfig* activeConfig,
        int updateIntervalMs
    ) {
        using namespace vehicle_sim;

        std::cout << "\nStarting " << vehicleType << " telemetry simulation\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        if (activeConfig) {
            presentation::printTelemetryHeader(std::cout, *activeConfig);
        }

        VehicleSimulator sim;
        sim.initialize();
        sim.start();

        int signalCount = 0;
        auto lastTime = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(updateIntervalMs);

        while (g_running) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);

            if (elapsed >= interval) {
                sim.update();
                presentation::printTelemetryRow(std::cout, sim.getLatestSignal(), ++signalCount);
                lastTime = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(SPIN_SLEEP_MS));
        }

        sim.stop();
        std::cout << "\n\nSimulation ended. Total signals generated: " << signalCount << "\n";
        std::cout << "Goodbye!\n";
        return 0;
    }

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
                      << "  5. Try moving closer to the vehicle (BLE range ~10m)\n";
            return 1;
        }

        std::cout << "\nFound " << devices.size() << " BLE device(s):\n\n";
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& dev = devices[i];
            std::cout << "  [" << (i + 1) << "] " << dev.name << "\n"
                      << "      Address: " << dev.address << "\n"
                      << "      Status: " << (dev.isConnected ? "Connected" : "Available") << "\n\n";
        }

        std::cout << "To connect to a device, use:\n"
                  << "  vehicle-sim --connect <address>\n";
        return 0;
    }

    int runConnect(
        vehicle_sim::BLEManager& bleManager,
        vehicle_sim::domain::DBCTranslationService& translationService,
        const std::string& address,
        const vehicle_sim::domain::VehicleConfig* activeConfig,
        int updateIntervalMs
    ) {
        using namespace vehicle_sim;

        std::cout << "\nAttempting to connect to: " << address << "\n";

        std::atomic<int> signalCount{0};
        std::atomic<bool> dataReceived{false};

        bleManager.onDataReceived([&](const std::vector<std::uint8_t>& data) {
            dataReceived = true;

            if (data.size() >= RAW_DATA_LOG_MIN_LENGTH) {
                std::cout << "[OBD2] Raw response: ";
                for (auto b : data) {
                    std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)b << " ";
                }
                std::cout << std::dec << std::endl;
            }

            auto signal = translationService.processFrame(data);
            if (signal) {
                presentation::printTelemetryRow(std::cout, *signal, ++signalCount);
            }
        });

        if (!bleManager.connect(address)) {
            std::cerr << "\nFailed to connect to BLE device: " << address << "\n\n"
                      << "Possible reasons:\n"
                      << "  - Device address is incorrect\n"
                      << "  - Device is out of range\n"
                      << "  - Device is already connected to another application\n"
                      << "  - OBD2 adapter lost power\n"
                      << "\nTry running --scan to verify device is available.\n";
            return 1;
        }

        std::cout << "Connected! Waiting for service discovery...\n";
        std::this_thread::sleep_for(std::chrono::seconds(SERVICE_DISCOVERY_WAIT_S));

        std::cout << "Initializing OBD2 adapter...\n";
        bleManager.initializeELM327();
        std::this_thread::sleep_for(std::chrono::seconds(ELM327_INIT_WAIT_S));

        // TODO: Auto-detect vehicle via VIN query here, then reload config

        std::cout << "Starting OBD2 data polling (interval: " << updateIntervalMs << "ms)...\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        if (activeConfig) {
            presentation::printTelemetryHeader(std::cout, *activeConfig);
        }

        bleManager.startOBD2Polling(updateIntervalMs);

        auto lastActivity = std::chrono::steady_clock::now();
        int noDataCount = 0;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(BLE_HEALTH_CHECK_MS));

            if (!bleManager.isConnected()) {
                std::cout << "\n[!] Connection lost.\n";
                break;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity).count();

            if (dataReceived) {
                lastActivity = now;
                dataReceived = false;
                noDataCount = 0;
            } else if (elapsed > NO_DATA_WARNING_S) {
                noDataCount++;
                if (noDataCount == 1) {
                    std::cout << "\n[!] No data received for " << elapsed << " seconds.\n"
                              << "[!] Ensure vehicle is ON and OBD2 adapter has power.\n";
                } else if (noDataCount == DRIVE_MODE_HINT_COUNT) {
                    std::cout << "[!] Still waiting. Some adapters need 'Drive' mode.\n";
                }
            }
        }

        bleManager.stopOBD2Polling();
        std::cout << "\nDisconnecting...\n";
        bleManager.disconnect();
        std::cout << "Total signals received: " << signalCount << "\nGoodbye!\n";
        return 0;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "vehicle-sim v1.0.0 - Vehicle OBD2 Telemetry Display\n";

    vehicle_sim::domain::DBCTranslationService translationService;
    vehicle_sim::domain::DefaultVehicleConfigs::registerAll(translationService.registry());

    auto opts = vehicle_sim::cli::parseArgs(argc, argv);

    if (!opts.error_message.empty()) {
        std::cerr << opts.error_message << "\n";
        return 1;
    }

    if (opts.help_requested) {
        vehicle_sim::cli::printHelp(std::cout, translationService.registry());
        return 0;
    }

    if (opts.list_signals) {
        vehicle_sim::cli::printSupportedSignals(std::cout, translationService.registry());
        return 0;
    }

    std::string vehicleType = opts.vehicle_type.empty()
        ? vehicle_sim::cli::DEFAULT_VEHICLE_TYPE
        : opts.vehicle_type;

    if (!translationService.loadVehicle(vehicleType, vehicle_sim::domain::VehicleProtocol::OBD2)) {
        std::cerr << "Failed to load vehicle config: " << vehicleType << "\n";
        return 1;
    }

    const auto* activeConfig = translationService.registry().getConfig(vehicleType);
    auto bleManager = std::make_unique<vehicle_sim::BLEManager>();

    if (opts.simulate_mode) {
        return runSimulation(vehicleType, activeConfig, opts.update_interval_ms);
    }

    if (opts.scan_mode) {
        return runScan(*bleManager);
    }

    if (opts.connect_mode) {
        return runConnect(*bleManager, translationService, opts.connect_address, activeConfig, opts.update_interval_ms);
    }

    vehicle_sim::cli::printHelp(std::cout, translationService.registry());
    return 0;
}
