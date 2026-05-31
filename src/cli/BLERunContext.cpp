#include "vehicle-sim/cli/BLERunContext.h"
#include "vehicle-sim/cli/BLEConnectionManager.h"
#include "vehicle-sim/cli/Orchestration.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/domain/VehicleDetector.h"
#include <iostream>
#include <iomanip>
#include <thread>

namespace {
    std::atomic<bool> g_running(true);

    void signalHandler(int sigNum) {
        std::cout << "\nReceived signal " << sigNum << ", shutting down..." << std::endl;
        g_running = false;
    }

    constexpr int BLE_HEALTH_CHECK_MS = 500;
    constexpr int NO_DATA_WARNING_S = 5;
    constexpr int DRIVE_MODE_HINT_COUNT = 3;
}

namespace vehicle_sim::cli {

int BLERunContext::run(const std::string& address,
                        const std::string& vehicleType,
                        domain::DBCTranslationService& translationService) {
    ::signal(SIGINT, signalHandler);
    ::signal(SIGTERM, signalHandler);

    if (vehicleType == "auto") {
        return runWithAutoDetection(address, translationService);
    }

    auto context = resolveVehicleContext(vehicleType, translationService);
    return runWithProtocol(address, context.protocol, translationService);
}

int BLERunContext::runWithAutoDetection(const std::string& address,
                                          domain::DBCTranslationService& translationService) {
    std::cout << "[Auto-detect] Connecting to BLE adapter for VIN detection..." << std::endl;

    auto bleManager = std::make_unique<BLEManager>();

    // Connect BLE adapter
    if (!bleManager->connect(address)) {
        std::cerr << "\nFailed to connect to BLE device: " << address << "\n"
                  << "Try running --scan to verify device is available.\n";
        return 1;
    }

    if (!bleManager->waitForCharacteristics()) {
        std::cerr << "\nFailed to discover BLE characteristics.\n";
        bleManager->disconnect();
        return 1;
    }

    // Initialize ELM327 for VIN query (safe: ATSP6, no protocol probing)
    if (!bleManager->initializeForVINQuery()) {
        std::cerr << "[Auto-detect] ELM327 initialization failed.\n";
        bleManager->disconnect();
        return 1;
    }

    // Query VIN
    auto vin = bleManager->queryVIN();
    bleManager->disconnect();

    if (!vin.has_value() || vin->empty()) {
        std::cerr << "\nCould not auto-detect vehicle. No VIN response received.\n"
                  << "Use --vehicle <type> to specify manually.\n\n"
                  << "Available vehicles:\n";
        for (const auto& id : translationService.registry().getRegisteredVehicles()) {
            const auto* cfg = translationService.registry().getConfig(id);
            if (cfg) std::cerr << "  " << id << "  (" << cfg->vehicleName << ")\n";
        }
        std::cerr << "\n";
        return 1;
    }

    // Decode VIN → vehicle config
    std::string wmi = vin->substr(0, 3);
    auto make = domain::VehicleDetector::decodeWMI(wmi);
    std::string configId = domain::VehicleDetector::makeToConfigId(make, false);

    std::cout << "[Auto-detect] VIN: " << *vin << "\n"
              << "[Auto-detect] WMI: " << wmi << " → "
              << static_cast<int>(make) << " → config: " << configId << "\n";

    if (configId.empty() || !translationService.registry().hasConfig(configId)) {
        std::cerr << "\nCould not auto-detect vehicle. WMI '" << wmi
                  << "' not mapped to a supported vehicle.\n"
                  << "Use --vehicle <type> to specify manually.\n\n"
                  << "Available vehicles:\n";
        for (const auto& id : translationService.registry().getRegisteredVehicles()) {
            const auto* cfg = translationService.registry().getConfig(id);
            if (cfg) std::cerr << "  " << id << "  (" << cfg->vehicleName << ")\n";
        }
        std::cerr << "\n";
        return 1;
    }

    std::cout << "[Auto-detect] Detected: " << configId << "\n";

    auto context = resolveVehicleContext(configId, translationService);
    return runWithProtocol(address, context.protocol, translationService);
}

int BLERunContext::runWithProtocol(const std::string& address,
                                     domain::VehicleProtocol protocol,
                                     domain::DBCTranslationService& translationService) {
    auto bleManager = std::make_unique<BLEManager>();
    BLEConnectionManager connMgr(std::move(bleManager));

    MonitorStats stats;

    if (!connMgr.connect(address, protocol,
        [&](const std::vector<std::uint8_t>& data) {
            stats.dataReceived = true;

            auto signal = translationService.processFrame(data);
            if (signal) {
                ++stats.signalCount;
            }
        })) {
        return 1;
    }

    connMgr.startPolling(500);

    auto lastActivity = std::chrono::steady_clock::now();

    while (connMgr.isConnected() && !connMgr.connectionLost() && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(BLE_HEALTH_CHECK_MS));

        {
            int count = connMgr.notificationCount();
            std::string hex = connMgr.lastRawHex();
            std::cout << "\r[BLE] notifications: " << count
                      << "  last: " << std::left << std::setw(52) << hex
                      << std::flush;
        }

        if (!stats.detectionShown && connMgr.vehicleDetector()->getResult().frameCount > 0) {
            std::cout << "\n";
            auto detResult = connMgr.vehicleDetector()->getResult();
            std::cout << "[VehicleDetector] " << detResult.evidenceSummary << "\n";
            if (detResult.hasSuggestion()) {
                std::cout << "[VehicleDetector] Suggesting: " << detResult.suggestedVehicleId
                          << " (confidence: " << static_cast<int>(detResult.confidence) << ")\n";
            }
            stats.detectionShown = true;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity).count();

        if (stats.dataReceived) {
            lastActivity = now;
            stats.dataReceived = false;
            stats.noDataCount = 0;
        } else if (elapsed > NO_DATA_WARNING_S) {
            stats.noDataCount++;
            if (stats.noDataCount == 1) {
                std::cout << "\n[!] No data received for " << elapsed << " seconds.\n"
                              << "[!] Ensure vehicle is ON and OBD2 adapter has power.\n";
            } else if (stats.noDataCount == DRIVE_MODE_HINT_COUNT) {
                std::cout << "[!] Still waiting. Some adapters need 'Drive' mode.\n";
            }
        }
    }

    connMgr.stopPolling();
    connMgr.disconnect();
    std::cout << "Total signals received: " << stats.signalCount << "\nGoodbye!\n";
    return 0;
}

} // namespace vehicle_sim::cli
