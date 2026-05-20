#pragma once
#include "vehicle-sim/domain/VehicleConfig.h"

namespace vehicle_sim::domain {

class DefaultVehicleConfigs {
public:
    static VehicleConfig generic();
    static VehicleConfig teslaModel3();
    static VehicleConfig audiMLBEvo();
    static void registerAll(VehicleConfigRegistry& registry);
};

} // namespace vehicle_sim::domain
