#import "VehicleSimWrapper.h"
#include "vehicle-sim/VehicleSim.h"
#include <memory>

@implementation VehicleSimWrapper {
    std::unique_ptr<vehicle_sim::VehicleSimulator> _simulator;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _simulator = std::make_unique<vehicle_sim::VehicleSimulator>();
    }
    return self;
}

- (void)start {
    _simulator->start();
}

- (void)stop {
    _simulator->stop();
}

- (void)update {
    _simulator->update();
}

- (double)throttlePercent {
    return _simulator->getLatestSignal().getThrottlePercent();
}

- (double)speedKmh {
    return _simulator->getLatestSignal().getSpeedKmh();
}

- (double)accelerationG {
    return _simulator->getLatestSignal().getAccelerationG();
}

- (double)brakePercent {
    return _simulator->getLatestSignal().getBrakePercent();
}

- (BOOL)isRunning {
    return _simulator->hasNewData();
}

@end
