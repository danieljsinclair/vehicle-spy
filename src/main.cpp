#include <iostream>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/cli/TelemetryRunner.h"
#include "vehicle-sim/cli/BLERunContext.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/SignalSourceFactory.h"

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

} // namespace

int main(int argc, char* argv[]) {
    using namespace vehicle_sim;

    cli::registerSignalHandlers();
    cli::printBanner();

    domain::DBCTranslationService translationService;
    domain::DefaultVehicleConfigs::registerAll(translationService.registry());

    auto opts = cli::parseArgs(argc, argv);
    if (!opts.error_message.empty()) {
        std::cerr << opts.error_message << "\n";
        return 1;
    }

    auto validationError = cli::validateOptions(opts, translationService);
    if (!validationError.empty()) {
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

    if (opts.isBLE()) {
        return cli::BLERunContext::run(opts.connect_target, opts.vehicle_type,
                                      translationService);
    }

    auto vehicleContext = cli::resolveVehicleContext(opts.vehicle_type, translationService);

    auto source = domain::SignalSourceFactory::create("demo",
                                                       opts.update_interval_ms);
    return cli::TelemetryRunner::run(std::move(source), vehicleContext.config,
                                      opts.log_csv, opts.log_raw,
                                      opts.update_interval_ms);
}