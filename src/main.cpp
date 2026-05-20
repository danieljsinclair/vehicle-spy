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
#include "vehicle-sim/domain/EventDispatcher.h"
#include "vehicle-sim/telemetry/TraceLogger.h"
#include "vehicle-sim/telemetry/RawTraceLogger.h"
#include "vehicle-sim/boundary/OBD2Protocol.h"
#include "vehicle-sim/domain/DemoSignalProvider.h"

namespace {
    std::atomic<bool> g_running(true);

    // Sleep between spin-loop iterations.
    // 10ms prevents busy-wait CPU burn while keeping response latency low.
    constexpr int SPIN_SLEEP_MS = 10;

    // BLE scan duration. 10s is enough to discover nearby OBD2 adapters.
    constexpr int BLE_SCAN_TIMEOUT_S = 10;

    // Wait after BLE connect for GATT service enumeration on ELM327.

    // Connection health check interval.
    constexpr int BLE_HEALTH_CHECK_MS = 500;

    // Seconds without data before warning.
    constexpr int NO_DATA_WARNING_S = 5;

    // Repeated warnings before "Drive mode" hint (~15s total).
    constexpr int DRIVE_MODE_HINT_COUNT = 3;

    // Minimum raw data length to log as hex.
    constexpr std::size_t RAW_DATA_LOG_MIN_LENGTH = 3;

    /**
     * Telemetry pipeline setup - DRY helper for common logging infrastructure
     *
     * Encapsulates the repeated pattern of:
     * - Creating EventDispatcher
     * - Optionally setting up CSV logger
     * - Optionally setting up raw logger
     * - Registering console output consumer
     */
    struct TelemetryPipeline {
        vehicle_sim::domain::EventDispatcher dispatcher;
        std::unique_ptr<vehicle_sim::telemetry::TraceLogger> csvLogger;
        std::unique_ptr<vehicle_sim::telemetry::RawTraceLogger> rawLogger;
        int dispatchCount_ = 0;

        /**
         * Setup telemetry logging infrastructure
         *
         * @param logCsvPath Path for CSV log (empty to disable)
         * @param logRawPath Path for raw hex log (empty to disable)
         * @param outStream Output stream for console display
         * @return true if setup succeeded, false on error
         */
        bool setup(const std::string& logCsvPath,
                   const std::string& logRawPath,
                   std::ostream& outStream) {
            using namespace vehicle_sim;

            // Setup CSV logging if requested
            if (!logCsvPath.empty()) {
                csvLogger = std::make_unique<telemetry::TraceLogger>(logCsvPath);
                if (!csvLogger->isValid()) {
                    std::cerr << "Failed to open CSV log file: " << logCsvPath << "\n";
                    return false;
                }
                dispatcher.registerConsumer([this](const domain::VehicleSignal& signal) {
                    (*csvLogger)(signal);
                });
            }

            // Setup raw logging if requested
            if (!logRawPath.empty()) {
                rawLogger = std::make_unique<telemetry::RawTraceLogger>(logRawPath);
                if (!rawLogger->isValid()) {
                    std::cerr << "Failed to open raw log file: " << logRawPath << "\n";
                    return false;
                }
            }

            // Always register console output
            dispatcher.registerConsumer([this, &outStream](const domain::VehicleSignal& signal) {
                presentation::printTelemetryRow(outStream, signal, ++dispatchCount_);
            });

            return true;
        }
    };

    void signalHandler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
        g_running = false;
    }

    int runSimulation(
        const std::string& vehicleType,
        const vehicle_sim::domain::VehicleConfig* activeConfig,
        int updateIntervalMs,
        const std::string& logCsvPath,
        const std::string& logRawPath
    ) {
        using namespace vehicle_sim;

        std::cout << "\nStarting " << vehicleType << " telemetry simulation\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        if (activeConfig) {
            presentation::printTelemetryHeader(std::cout, *activeConfig);
        }

        TelemetryPipeline pipeline;
        if (!pipeline.setup(logCsvPath, logRawPath, std::cout)) {
            return 1;
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
                auto signal = sim.getLatestSignal();
                ++signalCount;

                if (pipeline.rawLogger) {
                    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
                    pipeline.rawLogger->write(timestamp, {0});
                }

                pipeline.dispatcher.dispatch(signal);
                lastTime = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(SPIN_SLEEP_MS));
        }

        sim.stop();
        std::cout << "\n\nSimulation ended. Total signals generated: " << signalCount << "\n";
        std::cout << "Goodbye!\n";
        return 0;
    }

    int runConnectDemo(
        const std::string& vehicleType,
        const vehicle_sim::domain::VehicleConfig* activeConfig,
        int updateIntervalMs,
        const std::string& logCsvPath,
        const std::string& logRawPath
    ) {
        using namespace vehicle_sim;

        std::cout << "\nStarting " << vehicleType << " demo telemetry\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        if (activeConfig) {
            presentation::printTelemetryHeader(std::cout, *activeConfig);
        }

        TelemetryPipeline pipeline;
        if (!pipeline.setup(logCsvPath, logRawPath, std::cout)) {
            return 1;
        }

        domain::DemoSignalProvider demoProvider(updateIntervalMs);
        demoProvider.start([&](const domain::VehicleSignal& signal) {
            auto now = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            if (pipeline.rawLogger) {
                pipeline.rawLogger->write(timestamp, {0});
            }

            pipeline.dispatcher.dispatch(signal);
        });

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(SPIN_SLEEP_MS));
        }

        demoProvider.stop();
        std::cout << "\n\nDemo ended. Goodbye!\n";
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
                              << "  5. Try moving closer to vehicle (BLE range ~10m)\n";
            return 1;
        }

        std::cout << "\nFound " << devices.size() << " BLE device(s).\n";
        std::cout << "To connect: vehicle-sim --connect <address>\n\n";
        return 0;
    }

    int runConnect(
        vehicle_sim::BLEManager& bleManager,
        vehicle_sim::domain::DBCTranslationService& translationService,
        const std::string& address,
        const vehicle_sim::domain::VehicleConfig* activeConfig,
        int updateIntervalMs,
        vehicle_sim::domain::VehicleProtocol protocol,
        const std::string& logCsvPath,
        const std::string& logRawPath
    ) {
        using namespace vehicle_sim;

        std::cout << "\nAttempting to connect to: " << address << "\n";

        std::atomic<int> signalCount{0};
        std::atomic<bool> dataReceived{false};

        const char* protocolLabel = (protocol == domain::VehicleProtocol::CAN) ? "CAN" : "OBD2";

        TelemetryPipeline pipeline;
        if (!pipeline.setup(logCsvPath, logRawPath, std::cout)) {
            return 1;
        }

        bleManager.onDataReceived([&](const std::vector<std::uint8_t>& data) {
            dataReceived = true;

            if (pipeline.rawLogger) {
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                pipeline.rawLogger->write(timestamp, data);
            }

            if (data.size() >= RAW_DATA_LOG_MIN_LENGTH) {
                std::cout << "[" << protocolLabel << "] Raw response: ";
                for (auto b : data) {
                    std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)b << " ";
                }
                std::cout << std::dec << std::endl;
            }

            auto signal = translationService.processFrame(data);
            if (signal) {
                pipeline.dispatcher.dispatch(*signal);
                ++signalCount;
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

        std::cout << "Connected! Waiting for service/characteristic discovery...\n";

        if (!bleManager.waitForCharacteristics(10000)) {
            std::cerr << "\nFailed to discover required BLE characteristics.\n"
                      << "Check that the OBD2 adapter is powered and in range.\n";
            bleManager.disconnect();
            return 1;
        }
        std::cout << "Characteristics ready.\n";
        if (protocol == domain::VehicleProtocol::CAN) {
            std::cout << "Initializing CAN monitor mode...\n";
            if (!bleManager.initializeCANMonitor()) {
                std::cerr << "Failed to initialize ELM327 for CAN monitoring\n";
                return 1;
            }
        } else {
            std::cout << "Initializing OBD2 adapter...\n";
            if (!bleManager.initializeELM327()) {
                std::cerr << "Failed to initialize ELM327 adapter\n";
                return 1;
            }
        }

        std::cout << "Starting " << protocolLabel << " data polling (interval: " << updateIntervalMs << "ms)...\n";
        std::cout << "Press Ctrl+C to stop\n";
        if (activeConfig) {
            presentation::printTelemetryHeader(std::cout, *activeConfig);
        }

        // Use appropriate polling method based on protocol
        if (protocol == domain::VehicleProtocol::CAN) {
            bleManager.startCANMonitor(updateIntervalMs);
        } else {
            bleManager.startOBD2Polling(updateIntervalMs);
        }

        auto lastActivity = std::chrono::steady_clock::now();
        int noDataCount = 0;
        bool detectionShown = false;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(BLE_HEALTH_CHECK_MS));

            // Raw BLE activity status line (overwrites with \r)
            {
                int count = bleManager.bleNotificationCount();
                std::string hex = bleManager.lastRawHex();
                std::cout << "\r[BLE] notifications: " << count
                          << "  last: " << std::left << std::setw(52) << hex
                          << std::flush;
            }

            // Show detection diagnostics once we have data
            if (!detectionShown && bleManager.vehicleDetector()->getResult().frameCount > 0) {
                std::cout << "\n";
                auto detResult = bleManager.vehicleDetector()->getResult();
                std::cout << "[VehicleDetector] " << detResult.evidenceSummary << "\n";
                if (detResult.hasSuggestion()) {
                    std::cout << "[VehicleDetector] Suggesting: " << detResult.suggestedVehicleId
                              << " (confidence: " << static_cast<int>(detResult.confidence) << ")\n";
                }
                detectionShown = true;
            }

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

        if (protocol == domain::VehicleProtocol::CAN) {
            bleManager.stopCANMonitor();
        } else {
            bleManager.stopOBD2Polling();
        }
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

    // Get the vehicle config to determine protocol (OCP: no hard-coded type checking)
    const auto* activeConfig = translationService.registry().getConfig(vehicleType);
    if (!activeConfig) {
        std::cerr << "Vehicle config not found: " << vehicleType << "\n";
        auto vehicles = translationService.registry().getRegisteredVehicles();
        std::cerr << "Available vehicles: ";
        for (const auto& v : vehicles) {
            std::cerr << v << " ";
        }
        std::cerr << "\n";
        return 1;
    }

    // Determine protocol from config (not from hard-coded vehicle type names)
    vehicle_sim::domain::VehicleProtocol protocol = activeConfig->isCANProtocol
        ? vehicle_sim::domain::VehicleProtocol::CAN
        : vehicle_sim::domain::VehicleProtocol::OBD2;

    if (!translationService.loadVehicle(vehicleType, protocol)) {
        std::cerr << "Failed to load vehicle config: " << vehicleType << "\n";
        return 1;
    }
    auto bleManager = std::make_unique<vehicle_sim::BLEManager>();

    if (opts.simulate_mode) {
        return runSimulation(vehicleType, activeConfig, opts.update_interval_ms, opts.log_csv, opts.log_raw);
    }

    if (opts.connect_demo) {
        if (opts.vehicle_type.empty()) {
            std::cerr << "No vehicle specified. Use --vehicle <name>. Available: ";
            auto vehicles = translationService.registry().getRegisteredVehicles();
            for (const auto& v : vehicles) {
                std::cerr << v << " ";
            }
            std::cerr << "\n";
            return 1;
        }
        return runConnectDemo(vehicleType, activeConfig, opts.update_interval_ms, opts.log_csv, opts.log_raw);
    }

    if (opts.scan_mode) {
        return runScan(*bleManager);
    }

    if (opts.connect_mode) {
        return runConnect(*bleManager, translationService, opts.connect_address, activeConfig, opts.update_interval_ms, protocol, opts.log_csv, opts.log_raw);
    }

    vehicle_sim::cli::printHelp(std::cout, translationService.registry());
    return 0;
}
