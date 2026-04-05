/**
 * @brief Objective-C++ bridge between C++ core and Swift UI.
 *
 * This class exposes the vehicle-sim C++ API to Swift in a type-safe manner.
 * It must be compiled as Objective-C++ (.mm) to link against the C++ library.
 *
 * Usage from Swift:
 *   let wrapper = VehicleSimWrapper()
 *   let data = wrapper.getTelemetry()
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Immutable telemetry data for Swift consumption
@interface TelemetryData : NSObject

@property (nonatomic, readonly) NSTimeInterval timestamp;
@property (nonatomic, readonly) double rpm;
@property (nonatomic, readonly) double speedKmh;
@property (nonatomic, readonly) double throttlePercent;
@property (nonatomic, readonly) double brakePercent;
@property (nonatomic, readonly) NSInteger gear;
@property (nonatomic, readonly) double torque;
@property (nonatomic, readonly) double accelerationG;

- (instancetype)initWithTimestamp:(NSTimeInterval)timestamp
                              rpm:(double)rpm
                        speedKmh:(double)speedKmh
                throttlePercent:(double)throttlePercent
                   brakePercent:(double)brakePercent
                           gear:(NSInteger)gear
                         torque:(double)torque
                accelerationG:(double)accelerationG;

@end

/// Objective-C++ wrapper for vehicle-sim C++ core
@interface VehicleSimWrapper : NSObject

/// Initialize the simulator (loads default configuration)
- (instancetype)init;

/// Start telemetry capture (connects to BLE mock)
- (void)start;

/// Stop telemetry capture
- (void)stop;

/// Get the latest telemetry snapshot (blocking call for simplicity)
/// Returns nil if no data available
- (nullable TelemetryData *)getTelemetry;

/// Check if simulator is running
- (BOOL)isRunning;

@end

NS_ASSUME_NONNULL_END
