#include <iostream>
#include <iomanip>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include "vehicle-sim/VehicleSim.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/TelemetryFormatter.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/TeslaSignalParser.h"
#include "vehicle-sim/domain/SignalTranslatorFactory.h"
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace {
    std::atomic<bool> g_running(true);

    void signalHandler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
        g_running = false;
    }

    void printHelp() {
        std::cout << "vehicle-sim - Vehicle OBD2 Telemetry Display\n\n";
        std::cout << "USAGE:\n";
        std::cout << "  vehicle-sim [OPTIONS]\n\n";
        std::cout << "OPTIONS:\n";
        std::cout << "  --scan              Scan for BLE OBD2 adapters\n";
        std::cout << "  --connect <addr>    Connect to specific BLE adapter address\n";
        std::cout << "  --vehicle <type>    Vehicle type: generic (default) or tesla\n";
        std::cout << "  --list              List supported signals\n";
        std::cout << "  --format <fmt>      Output format: json, csv, or plain (default: plain)\n";
        std::cout << "  --interval <ms>     Update interval in milliseconds (default: 500)\n";
        std::cout << "  --simulate          Demo mode with mock telemetry (no hardware needed)\n";
        std::cout << "  --help              Show this help message\n\n";
        std::cout << "VEHICLE TYPES:\n";
        std::cout << "  generic   Standard OBD2 (SAE J1979) - any car with ELM327 adapter\n";
        std::cout << "  tesla     Tesla Model 3/Y CAN bus format\n\n";
        std::cout << "EXAMPLES:\n";
        std::cout << "  vehicle-sim --simulate\n";
        std::cout << "  vehicle-sim --scan\n";
        std::cout << "  vehicle-sim --connect <addr> --vehicle generic\n";
        std::cout << "  vehicle-sim --connect <addr> --vehicle tesla --interval 200\n";
        std::cout << "\nREQUIREMENTS:\n";
        std::cout << "  For real data: Connect a BLE OBD2 adapter to your vehicle's OBD-II port.\n";
        std::cout << "  Use --vehicle generic for standard OBD2 (most cars), or --vehicle tesla\n";
        std::cout << "  for Tesla-specific CAN bus format.\n";
    }

    void printSupportedSignals(const std::string& vehicleType) {
        if (vehicleType == "tesla") {
            std::cout << "\nSupported Tesla Model Y Signals:\n";
        } else {
            std::cout << "\nSupported OBD2 Signals (SAE J1979):\n";
        }
        std::cout << "  Throttle Position  - 0-100% (accelerator pedal)\n";
        std::cout << "  Vehicle Speed     - km/h\n";
        std::cout << "  Brake Pressure    - 0-100%\n";
        std::cout << "  Acceleration     - G-force (lateral/longitudinal)\n\n";
        if (vehicleType == "tesla") {
            std::cout << "These signals are parsed from Tesla CAN bus data via OBD-II port.\n";
        } else {
            std::cout << "Standard OBD2 PIDs read via ELM327 BLE adapter.\n";
        }
    }

    void printTelemetryHeader(const std::string& vehicleType) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << (vehicleType == "tesla" ? "Tesla" : "Vehicle");
        std::cout << " Real-Time Telemetry\n";
        std::cout << std::string(70, '=') << "\n\n";
    }

    void printTelemetryRow(const vehicle_sim::domain::VehicleSignal& signal, int count) {
        std::cout << "[" << count << "] ";
        std::cout << "Throttle: " << std::setw(5) << std::fixed << std::setprecision(1)
                  << signal.getThrottlePercent() << "%  ";
        std::cout << "Speed: " << std::setw(5) << std::fixed << std::setprecision(1)
                  << signal.getSpeedKmh() << " km/h  ";
        std::cout << "Brake: " << std::setw(5) << std::fixed << std::setprecision(1)
                  << signal.getBrakePercent() << "%  ";
        std::cout << "Accel: " << std::setw(5) << std::fixed << std::setprecision(2)
                  << signal.getAccelerationG() << " G\n";
    }
}

int main(int argc, char* argv[]) {
    // Setup signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "vehicle-sim v1.0.0 - Vehicle OBD2 Telemetry Display\n";

    // Parse command line arguments
    bool scan_mode = false;
    bool connect_mode = false;
    bool list_signals = false;
    bool simulate_mode = false;
    std::string connect_address;
    std::string format = "plain";
    std::string vehicle_type = "generic";  // Default to generic OBD2
    int update_interval_ms = 500;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--scan" || arg == "-s") {
            scan_mode = true;
        } else if (arg == "--connect" || arg == "-c") {
            if (i + 1 < argc) {
                connect_mode = true;
                connect_address = argv[++i];
            } else {
                std::cerr << "Error: --connect requires an address argument\n";
                return 1;
            }
        } else if (arg == "--vehicle" || arg == "-v") {
            if (i + 1 < argc) {
                vehicle_type = argv[++i];
            } else {
                std::cerr << "Error: --vehicle requires a type argument (generic or tesla)\n";
                return 1;
            }
        } else if (arg == "--list" || arg == "-l") {
            list_signals = true;
        } else if (arg == "--format" || arg == "-f") {
            if (i + 1 < argc) {
                format = argv[++i];
            } else {
                std::cerr << "Error: --format requires a format argument\n";
                return 1;
            }
        } else if (arg == "--interval" || arg == "-i") {
            if (i + 1 < argc) {
                update_interval_ms = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: --interval requires a number argument\n";
                return 1;
            }
        } else if (arg == "--simulate" || arg == "-m") {
            simulate_mode = true;
        } else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
    }

    // Handle list signals mode
    if (list_signals) {
        printSupportedSignals(vehicle_type);
        return 0;
    }

    // Create signal translator via factory (DI)
    vehicle_sim::domain::SignalTranslatorFactory translatorFactory;
    auto translator = translatorFactory.create(vehicle_type);

    // Initialize BLE components for scan and connect modes
    auto ble_manager = std::make_unique<vehicle_sim::BLEManager>();
    auto signal_parser = std::make_unique<vehicle_sim::domain::TeslaSignalParser>(
        [](const vehicle_sim::domain::VehicleSignal& signal) {}
    );

    // Handle simulation mode
    if (simulate_mode) {
        std::cout << "\nStarting " << vehicle_type << " telemetry simulation (10Hz update rate)\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        printTelemetryHeader(vehicle_type);

        int signal_count = 0;
        auto last_time = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(update_interval_ms);

        while (g_running) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);

            if (elapsed >= interval) {
                // Generate mock telemetry
                float throttle = 30.0f + (rand() % 40); // 30-70%
                float speed = 40.0f + (rand() % 30);   // 40-70 km/h
                float brake = 0.0f;
                float accel = 0.1f + (rand() % 10) / 100.0f; // 0.1-0.2 G

                // Create mock signal using constructor
                vehicle_sim::domain::VehicleSignal signal(
                    throttle,
                    speed,
                    accel,
                    brake,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()
                    ).count()
                );

                printTelemetryRow(signal, ++signal_count);

                last_time = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "\n\nSimulation ended. Total signals generated: " << signal_count << "\n";
        std::cout << "Goodbye!\n";
        return 0;
    }

    // Scan mode
    if (scan_mode) {
        std::cout << "\nScanning for BLE devices (10 seconds)...\n";
        std::cout << "Note: Make sure your vehicle is ON and OBD2 adapter is connected.\n\n";

        auto devices = ble_manager->scanForDevices(10);

        if (devices.empty()) {
            std::cout << "\nNo BLE devices found.\n\n";
            std::cout << "Troubleshooting:\n";
            std::cout << "  1. Ensure your vehicle is powered ON (accessory mode or drive mode)\n";
            std::cout << "  2. Verify OBD2 adapter is connected to vehicle's OBD-II port\n";
            std::cout << "  3. Check adapter has power (some require external power)\n";
            std::cout << "  4. On macOS, grant Bluetooth permissions if prompted\n";
            std::cout << "  5. Try moving closer to the vehicle (BLE range ~10m)\n";
            return 1;
        }

        std::cout << "\nFound " << devices.size() << " BLE device(s):\n\n";
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& dev = devices[i];
            std::cout << "  [" << (i + 1) << "] " << dev.name << "\n";
            std::cout << "      Address: " << dev.address << "\n";
            std::cout << "      Status: " << (dev.isConnected ? "Connected" : "Available") << "\n\n";
        }

        std::cout << "To connect to a device, use:\n";
        std::cout << "  vehicle-sim --connect <address>\n";
        return 0;
    }

    // Connect mode
    if (connect_mode) {
        std::cout << "\nAttempting to connect to: " << connect_address << "\n";

        // Register data callback before connecting
        std::atomic<int> signal_count{0};
        std::atomic<bool> data_received{false};

        ble_manager->onDataReceived([&](const std::vector<std::uint8_t>& data) {
            data_received = true;

            // Parse OBD2 response and display raw data
            if (data.size() >= 3) {
                std::cout << "[OBD2] Raw response: ";
                for (auto b : data) {
                    std::cout << std::hex << std::setfill('0') << std::setw(2)
                              << (int)b << " ";
                }
                std::cout << std::dec << std::endl;

                // Try to parse as OBD2 response via base class
                auto platform = ble_manager->getPlatform();
                if (platform) {
                    auto response = platform->parseOBD2Response(data);
                    if (response.valid && response.value) {
                        std::cout << "[OBD2] PID 0x" << std::hex << (int)response.pid
                                  << std::dec << " = " << *response.value << "\n";
                    }
                }
            }

            // Also try Tesla signal parsing
            auto frames = signal_parser->feedData(data);
            if (!frames.empty()) {
                int count = ++signal_count;
                for (const auto& frame : frames) {
                    auto signal = signal_parser->parseFrame(frame);
                    if (signal) {
                        printTelemetryRow(*signal, count);
                    }
                }
            }
        });

        if (!ble_manager->connect(connect_address)) {
            std::cerr << "\nFailed to connect to BLE device: " << connect_address << "\n\n";
            std::cerr << "Possible reasons:\n";
            std::cerr << "  - Device address is incorrect\n";
            std::cerr << "  - Device is out of range\n";
            std::cerr << "  - Device is already connected to another application\n";
            std::cerr << "  - OBD2 adapter lost power\n";
            std::cerr << "\nTry running --scan to verify device is available.\n";
            return 1;
        }

        // Wait for service/characteristic discovery to complete
        std::cout << "Connected! Waiting for service discovery...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Initialize ELM327 adapter with AT commands
        std::cout << "Initializing OBD2 adapter...\n";
        ble_manager->initializeELM327();

        // Wait for adapter to process AT commands
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Start active OBD2 PID polling
        std::cout << "Starting OBD2 data polling (interval: " << update_interval_ms << "ms)...\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        printTelemetryHeader(vehicle_type);

        ble_manager->startOBD2Polling(update_interval_ms);

        // Main loop - keep running until user stops
        auto last_activity = std::chrono::steady_clock::now();
        int no_data_count = 0;

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            if (!ble_manager->isConnected()) {
                std::cout << "\n[!] Connection lost.\n";
                break;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_activity
            ).count();

            if (data_received) {
                last_activity = now;
                data_received = false;
                no_data_count = 0;
            } else if (elapsed > 5) {
                no_data_count++;
                if (no_data_count == 1) {
                    std::cout << "\n[!] No data received for " << elapsed << " seconds.\n";
                    std::cout << "[!] Ensure vehicle is ON and OBD2 adapter has power.\n";
                } else if (no_data_count == 3) {
                    std::cout << "[!] Still waiting. Some adapters need 'Drive' mode.\n";
                }
            }
        }

        // Cleanup
        ble_manager->stopOBD2Polling();
        std::cout << "\nDisconnecting...\n";
        ble_manager->disconnect();
        std::cout << "Total signals received: " << signal_count << "\n";
        std::cout << "Goodbye!\n";

        return 0;
    }

    // Default: show help
    printHelp();
    return 0;
}
