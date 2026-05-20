#include "vehicle-sim/cli/BLERunContext.h"
#include "vehicle-sim/BLEManager.h"
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
                        domain::VehicleProtocol protocol,
                        domain::DBCTranslationService& translationService) {
    // Register signal handlers for this run
    ::signal(SIGINT, signalHandler);
    ::signal(SIGTERM, signalHandler);

    auto bleManager = std::make_unique<BLEManager>();
    BLEConnectionManager connMgr(std::move(bleManager));

    MonitorStats stats;
    const char* protocolLabel = (protocol == domain::VehicleProtocol::CAN) ? "CAN" : "OBD2";

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

    connMgr.startPolling(500);  // Default polling interval

    auto lastActivity = std::chrono::steady_clock::now();

    while (connMgr.isConnected() && !connMgr.connectionLost() && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(BLE_HEALTH_CHECK_MS));

        // Raw BLE activity status line (overwrites with \r)
        {
            int count = connMgr.notificationCount();
            std::string hex = connMgr.lastRawHex();
            std::cout << "\r[BLE] notifications: " << count
                      << "  last: " << std::left << std::setw(52) << hex
                      << std::flush;
        }

        // Show detection diagnostics once we have data
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