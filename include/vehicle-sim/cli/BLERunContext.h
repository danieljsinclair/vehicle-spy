#pragma once

#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include <atomic>
#include <string>

namespace vehicle_sim::cli {

class BLERunContext {
public:
    static int run(
        const std::string& address,
        const std::string& vehicleType,
        domain::DBCTranslationService& translationService
    );

private:
    static int runWithAutoDetection(const std::string& address,
                                     domain::DBCTranslationService& translationService,
                                     const pipeline::StopToken& stop);
    static int runWithProtocol(const std::string& address,
                                domain::VehicleProtocol protocol,
                                const domain::DBCTranslationService& translationService,
                                const pipeline::StopToken& stop);

    struct MonitorStats {
        std::atomic<int> signalCount{0};
        std::atomic<bool> dataReceived{false};
        int noDataCount = 0;
        bool detectionShown = false;
    };
};

} // namespace vehicle_sim::cli