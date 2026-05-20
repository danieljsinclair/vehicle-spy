#pragma once

#include "vehicle-sim/cli/BLEConnectionManager.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include <atomic>
#include <chrono>
#include <memory>

namespace vehicle_sim::cli {

/**
 * BLE run context manager
 *
 * Handles the complete BLE execution flow including:
 * - Connection lifecycle (via BLEConnectionManager)
 * - Data translation via DBCTranslationService
 * - Health monitoring loop
 * - Vehicle detection diagnostics
 *
 * Keeps main() clean by encapsulating all BLE-specific logic.
 */
class BLERunContext {
public:
    /**
     * Run BLE telemetry
     *
     * @param address BLE device address
     * @param protocol Vehicle protocol
     * @param translationService DBC translation service
     * @return Exit code (0 on success, non-zero on error)
     */
    static int run(
        const std::string& address,
        domain::VehicleProtocol protocol,
        domain::DBCTranslationService& translationService
    );

private:
    struct MonitorStats {
        std::atomic<int> signalCount{0};
        std::atomic<bool> dataReceived{false};
        int noDataCount = 0;
        bool detectionShown = false;
    };
};

} // namespace vehicle_sim::cli