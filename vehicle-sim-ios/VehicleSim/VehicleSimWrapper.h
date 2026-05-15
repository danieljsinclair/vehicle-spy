#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// BLE device information for Swift
@interface VehicleSimDevice : NSObject
@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) NSString *address;
@property (nonatomic, assign) int rssi;
@end

/// Objective-C++ wrapper for vehicle-sim C++ core
/// Supports both demo simulation mode and live BLE data mode
@interface VehicleSimWrapper : NSObject

// MARK: - Demo Mode

/// Start demo simulation
- (void)startDemo;

/// Stop demo simulation
- (void)stopDemo;

// MARK: - BLE Live Mode

/// Scan for BLE devices
/// @param timeout Duration to scan in seconds
/// @return Array of discovered devices
- (NSArray<VehicleSimDevice*> *)scanForDevices:(NSTimeInterval)timeout;

/// Connect to a BLE device
/// @param address Device address to connect to
/// @param deviceName Display name of the BLE device
/// @param vehicleType Vehicle type (e.g., "tesla_model3", "audi_mlb_evo", "generic")
/// @return YES if connection initiated successfully
- (BOOL)connectToDevice:(NSString *)address deviceName:(NSString *)deviceName vehicleType:(NSString *)vehicleType;

/// Disconnect from current BLE device
- (void)disconnect;

/// Switch vehicle interpreter while connected
/// @param vehicleType New vehicle type (e.g., "tesla_model3", "audi_mlb_evo", "generic")
/// @return YES if switch succeeded
- (BOOL)switchVehicleType:(NSString *)vehicleType;

/// Advance demo simulation by one tick
- (void)updateSimulator;

// MARK: - Signal Values

/// Latest throttle percent (0.0 - 100.0), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *throttlePercent;

/// Latest speed in km/h (0.0 - 300.0), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *speedKmh;

/// Latest acceleration in G (-5.0 to +5.0), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *accelerationG;

/// Latest brake percent (0.0 - 100.0), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *brakePercent;

/// Latest motor RPM (0.0 - 20000.0), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *motorRpm;

/// Latest motor torque in Nm (-7500.0 to +7500.0), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *motorTorqueNm;

/// Latest gear selector ("P", "R", "N", "D", "S"), nil when no data
@property (nonatomic, readonly, nullable) NSString *gearSelector;

/// Latest steering angle in degrees (-819.2 to +819.2), nil when no data
@property (nonatomic, readonly, nullable) NSNumber *steeringAngleDeg;

// MARK: - State

/// YES if connected to BLE device
@property (nonatomic, readonly) BOOL isConnected;

/// YES if demo mode is active
@property (nonatomic, readonly) BOOL isDemoMode;

/// YES if BLE is ready for connections
@property (nonatomic, readonly) BOOL isBluetoothReady;

/// Name of the connected BLE adapter
@property (nonatomic, readonly, nullable) NSString *connectedDeviceName;

/// Address of the connected BLE adapter
@property (nonatomic, readonly, nullable) NSString *connectedDeviceAddress;

/// Vehicle detection diagnostic info
@property (nonatomic, readonly) NSString *detectionInfo;

/// Whether frames are actively being received (< 1s since last frame)
@property (nonatomic, readonly) BOOL isReceivingData;

/// Raw BLE notification count (increments on every BLE notification, before parsing)
@property (nonatomic, readonly) int bleNotificationCount;

/// Hex dump of last raw bytes received from BLE (before parsing)
@property (nonatomic, readonly) NSString *lastRawHex;

@end

NS_ASSUME_NONNULL_END
