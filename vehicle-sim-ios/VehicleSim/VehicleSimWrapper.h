#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Forward declaration
@protocol VehicleSimWrapperProtocol;

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

// MARK: - Protocol for Testability

@protocol VehicleSimWrapperProtocol <NSObject>

// MARK: - Connection Control
- (void)startDemo;
- (void)startBLE;
- (void)stop;
- (BOOL)connectToDevice:(NSString *)address deviceName:(NSString *)deviceName vehicleType:(NSString *)vehicleType;

// MARK: - BLE Scanning
- (NSArray<VehicleSimDevice *> *)scanForDevices:(NSTimeInterval)timeout;

// MARK: - Vehicle Options
- (NSArray<NSDictionary<NSString *, NSString *> *> *)getVehicleOptions;
- (BOOL)switchVehicleType:(NSString *)vehicleType;
- (void)disconnect;

// MARK: - Signal Values
@property (nonatomic, readonly, nullable) NSNumber *throttlePercent;
@property (nonatomic, readonly, nullable) NSNumber *speedKmh;
@property (nonatomic, readonly, nullable) NSNumber *accelerationG;
@property (nonatomic, readonly, nullable) NSNumber *brakePercent;
@property (nonatomic, readonly, nullable) NSNumber *motorRpm;
@property (nonatomic, readonly, nullable) NSNumber *motorTorqueNm;
@property (nonatomic, readonly, nullable) NSString *gearSelector;
@property (nonatomic, readonly, nullable) NSNumber *steeringAngleDeg;

// MARK: - State
@property (nonatomic, readonly) ConnectionState connectionState;
@property (nonatomic, readonly) BOOL isBluetoothReady;
@property (nonatomic, readonly, nullable) NSString *connectedDeviceName;
@property (nonatomic, readonly, nullable) NSString *connectedDeviceAddress;
@property (nonatomic, readonly) NSString *detectionInfo;
@property (nonatomic, readonly) BOOL isReceivingData;
@property (nonatomic, readonly) int bleNotificationCount;
@property (nonatomic, readonly) NSString *lastRawHex;

@end

/// Objective-C++ wrapper for vehicle-sim C++ core
/// Supports both demo simulation mode and live BLE data mode
@interface VehicleSimWrapper : NSObject <VehicleSimWrapperProtocol>

// MARK: - Initialization

/// Initialize with optional vehicle type
/// @param vehicleType Vehicle type (e.g., "tesla_model3", "audi_mlb_evo", "generic")
- (instancetype)initWithVehicleType:(nullable NSString *)vehicleType;

/// Initialize with default vehicle type
- (instancetype)init;

@end

NS_ASSUME_NONNULL_END