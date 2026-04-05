#import "VehicleSimWrapper.h"
#import "vehicle-sim/VehicleSim.h"
#import "vehicle-sim/ble/platform/BLEManagerMock.h"
#include <memory>

using namespace vehicle_sim;

@interface VehicleSimWrapper () {
    std::unique_ptr<VehicleSimulator> _simulator;
    std::unique_ptr<BLEManagerMock> _bleMock;
    bool _running;
}

@end

@implementation VehicleSimWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        _simulator = std::make_unique<VehicleSimulator>();
        _bleMock = std::make_unique<BLEManagerMock>();

        // Configure simulator with mock BLE platform
        // TODO: Proper setPlatform requires exposing BLEManager access
        _running = false;
    }
    return self;
}

- (void)start {
    if (_running) return;
    // TODO: Actually start simulator and BLE
    _running = true;
}

- (void)stop {
    if (!_running) return;
    // TODO: Stop simulator
    _running = false;
}

- (TelemetryData *)getTelemetry {
    // Placeholder: return mock data
    // In real implementation, query VehicleSimulator for latest PhysicsData
    // and convert to TelemetryData
    TelemetryData *data = [[TelemetryData alloc] initWithTimestamp:NSDate.timeIntervalSinceReferenceDate
                                                              rpm:2500.0
                                                        speedKmh:85.0
                                                throttlePercent:35.0
                                                   brakePercent:0.0
                                                           gear:4
                                                         torque:200.0
                                                accelerationG:0.2];
    return data;
}

- (BOOL)isRunning {
    return _running;
}

@end

@implementation TelemetryData

- (instancetype)initWithTimestamp:(NSTimeInterval)timestamp
                              rpm:(double)rpm
                        speedKmh:(double)speedKmh
                throttlePercent:(double)throttlePercent
                   brakePercent:(double)brakePercent
                           gear:(NSInteger)gear
                         torque:(double)torque
                accelerationG:(double)accelerationG {
    self = [super init];
    if (self) {
        _timestamp = timestamp;
        _rpm = rpm;
        _speedKmh = speedKmh;
        _throttlePercent = throttlePercent;
        _brakePercent = brakePercent;
        _gear = gear;
        _torque = torque;
        _accelerationG = accelerationG;
    }
    return self;
}

@end
