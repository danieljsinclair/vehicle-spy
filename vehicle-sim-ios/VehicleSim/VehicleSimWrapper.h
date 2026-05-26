#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Connection state enumeration for the vehicle simulation
typedef NS_ENUM(NSInteger, ConnectionState) {
    ConnectionStateDisconnected = 0,
    ConnectionStateConnecting,
    ConnectionStateConnected
};

/// BLE device information for Swift
@interface VehicleSimDevice : NSObject
@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) NSString *address;
@property (nonatomic, assign) int rssi;
@end

/// Objective-C++ wrapper for vehicle-sim C++ core
/// Supports both demo simulation mode and live BLE data mode
@interface VehicleSimWrapper : NSObject

// MARK: - Initialization

/// Initialize with optional vehicle type
/// @param vehicleType Vehicle type (e.g., "tesla_model3", "audi_mlb_evo", "generic")
- (instancetype)initWithVehicleType:(nullable NSString *)vehicleType;

/// Initialize with default vehicle type
- (instancetype)init;

// MARK: - Vehicle Configuration

/// Get available vehicle options from the registry
/// @return Array of dictionaries with "id" and "displayName" keys
- (NSArray<NSDictionary<NSString*, NSString*>*> *)getVehicleOptions;

// MARK: - Connection Control

/// Start BLE live mode
- (void)startBLE;

/// Stop current connection (demo or BLE)
- (void)stop;

/// Scan for BLE devices
/// @param timeout Duration to scan in seconds
/// @return Array of discovered devices
- (NSArray<VehicleSimDevice*> *)scanForDevices:(NSTimeInterval)timeout;

/// Connect to a BLE device
/// @param address Device address to connect to
/// @param deviceName Display name of the BLE device
/// @return YES if connection initiated successfully
- (BOOL)connectToDevice:(NSString *)address deviceName:(NSString *)deviceName;

/// Disconnect from current BLE device
- (void)disconnect;

/// Switch vehicle type while connected
/// @param vehicleType New vehicle type (e.g., "tesla_model3", "audi_mlb_evo", "generic")
/// @return YES if switch succeeded
- (BOOL)switchVehicleType:(NSString *)vehicleType;

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

/// Current connection state
@property (nonatomic, readonly) ConnectionState connectionState;

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