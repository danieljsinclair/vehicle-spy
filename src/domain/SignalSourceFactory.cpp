#include "vehicle-sim/domain/SignalSourceFactory.h"
#include "vehicle-sim/domain/DemoSignalSource.h"
#include "vehicle-sim/domain/SimulationSignalSource.h"
#include "vehicle-sim/VehicleSim.h"
#include <stdexcept>

namespace vehicle_sim::domain {

std::unique_ptr<ISignalSource> SignalSourceFactory::create(
    const std::string& sourceType,
    int updateIntervalMs
) {
    if (sourceType == "demo") {
        return std::make_unique<DemoSignalSource>(updateIntervalMs);
    }

    if (sourceType == "simulation") {
        return std::make_unique<SimulationSignalSource>(
            std::make_unique<VehicleSimulator>(), updateIntervalMs);
    }

    throw std::invalid_argument("Unknown source type: " + sourceType);
}

} // namespace vehicle_sim::domain