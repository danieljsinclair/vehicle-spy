#include "vehicle-sim/domain/SignalSourceFactory.h"
#include "vehicle-sim/domain/DemoSignalSource.h"
#include <stdexcept>

namespace vehicle_sim::domain {

std::unique_ptr<ISignalSource> SignalSourceFactory::create(
    const std::string& sourceType,
    int updateIntervalMs
) {
    if (sourceType == "demo") {
        return std::make_unique<DemoSignalSource>(updateIntervalMs);
    }

    throw std::invalid_argument("Unknown source type: " + sourceType);
}

} // namespace vehicle_sim::domain