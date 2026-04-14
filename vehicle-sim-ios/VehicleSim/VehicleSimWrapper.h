/**
 * @brief Objective-C++ thin bridge between C++ VehicleSimulator and Swift UI.
 *
 * Zero simulation logic — delegates entirely to C++ VehicleSimulator.
 * Only exposes the 4 VehicleSignal fields: throttle, speed, acceleration, brake.
 *
 * Usage from Swift:
 *   let wrapper = VehicleSimWrapper()
 *   wrapper.start()
 *   let t = wrapper.throttlePercent
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C++ wrapper for vehicle-sim C++ core
@interface VehicleSimWrapper : NSObject

/// Initialize the simulator (creates C++ VehicleSimulator)
- (instancetype)init;

/// Start simulation
- (void)start;

/// Stop simulation
- (void)stop;

/// Advance simulation by one tick and update signal values
- (void)update;

/// Latest signal values from C++ VehicleSignal (0.0 - 100.0)
@property (nonatomic, readonly) double throttlePercent;

/// Latest speed in km/h (0.0 - 300.0)
@property (nonatomic, readonly) double speedKmh;

/// Latest acceleration in G (-5.0 to +5.0)
@property (nonatomic, readonly) double accelerationG;

/// Latest brake percent (0.0 - 100.0)
@property (nonatomic, readonly) double brakePercent;

/// Check if simulator is running
- (BOOL)isRunning;

@end

NS_ASSUME_NONNULL_END
