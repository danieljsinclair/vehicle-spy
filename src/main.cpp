#include <iostream>
#include <memory>
#include "vehicle-sim/VehicleSim.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/TelemetryFormatter.h"

int main(int argc, char* argv[]) {
    std::cout << "vehicle-sim version 1.0.0" << std::endl;
    std::cout << "A command-line vehicle telemetry system for Tesla Model Y" << std::endl;
    std::cout << std::endl;

    // Show command-line arguments if any
    if (argc > 1) {
        std::cout << "Arguments:" << std::endl;
        for (int i = 1; i < argc; ++i) {
            std::cout << "  [" << i << "]: " << argv[i] << std::endl;
        }
        std::cout << std::endl;
    }

    // Demonstration of basic architecture
    std::cout << "Core components initialized:" << std::endl;
    std::cout << "  - VehicleSim (main orchestrator)" << std::endl;
    std::cout << "  - BLEManager (Tesla OBD2 BLE interface)" << std::endl;
    std::cout << "  - TelemetryFormatter (JSON/CSV output)" << std::endl;
    std::cout << std::endl;

    std::cout << "Run with --help for usage information." << std::endl;
    std::cout << std::endl;
    std::cout << "This is a project scaffolding. Implement your core logic in src/." << std::endl;

    return 0;
}
