#import "VehicleSimWrapper.h"
#include "vehicle-sim/VehicleSim.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include <memory>
#include <atomic>
#include <mutex>

using namespace vehicle_sim;
using namespace vehicle_sim::domain;

namespace {

const char* TESLA_MODEL3_DBC = R"DBC(VERSION ""

NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_

BS_:

BU_: DIR DI SCCM

BO_ 264 DIR_torque: 8 DIR
 SG_ DIR_axleSpeed : 40|16@1- (0.1,0) [-2750|2750] "RPM" DIR
 SG_ DIR_torqueActual : 27|13@1- (2,0) [-7500|7500] "Nm" DIR

BO_ 280 DI_state: 8 DI
 SG_ DI_accelPedalPos : 32|8@1+ (0.4,0) [0|100] "%" DI
 SG_ DI_brakePedalState : 19|2@1+ (1,0) [0|2] "" DI

BO_ 297 SCCM_steeringAngle: 5 SCCM
 SG_ SteeringAngle129 : 16|14@1+ (0.1,-819.2) [-819.2|819.1] "deg" SCCM

VAL_ 280 DI_brakePedalState 0 "released" 1 "pressed" ;
)DBC";

const char* AUDI_MLB_DBC = R"DBC(VERSION ""

NS_ :
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    BA_DEF_REL_
    BA_REL_
    BA_DEF_DEF_REL_
    BU_SG_REL_
    BU_EV_REL_
    BU_BO_REL_
    SG_MUL_VAL_

BS_:

BU_: ESP

BO_ 256 ESP_01: 8 ESP
 SG_ ESP_v_Signal : 0|16@1+ (0.01,0) [0|655.35] "km/h" ESP
 SG_ ESP_Laengsbeschl : 16|16@1- (0.01,-327.68) [-327.68|327.67] "m/s^2" ESP
 SG_ ESP_Bremsdruck : 32|8@1+ (0.4,0) [0|100] "%" ESP
)DBC";

std::string getEmbeddedDBC(const std::string& vehicleType) {
    if (vehicleType == "tesla_model3") return TESLA_MODEL3_DBC;
    if (vehicleType == "audi_mlb_evo") return AUDI_MLB_DBC;
    return ""; // generic uses OBD2, no DBC needed
}

} // anonymous namespace

// MARK: - VehicleSimDevice Implementation

@implementation VehicleSimDevice
@end

// MARK: - VehicleSimWrapper Implementation

@interface VehicleSimWrapper () {
    std::unique_ptr<vehicle_sim::VehicleSimulator> _simulator;
    std::unique_ptr<BLEManager> _bleManager;
    std::unique_ptr<DBCTranslationService> _translationService;

    // Thread-safe signal values (updated from BLE callback)
    std::optional<double> _throttlePercent;  // Protected by _mutex
    std::optional<double> _speedKmh;
    std::optional<double> _accelerationG;
    std::optional<double> _brakePercent;
    std::optional<double> _motorRpm;
    std::optional<double> _motorTorqueNm;
    std::optional<std::string> _gearSelector;
    std::optional<double> _steeringAngleDeg;

    // State
    std::atomic<bool> _isDemoMode;
    std::atomic<bool> _isConnected;

    // Mutex for protecting operations and _gearSelector
    std::mutex _mutex;

    // Vehicle protocol for current connection
    VehicleProtocol _protocol;

    // Connected device info
    NSString *_connectedDeviceName;
    NSString *_connectedDeviceAddress;
}

@end

@implementation VehicleSimWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        _simulator = std::make_unique<vehicle_sim::VehicleSimulator>();
        _bleManager = std::make_unique<BLEManager>();
        _translationService = std::make_unique<DBCTranslationService>();

        // Register default vehicle configs
        DefaultVehicleConfigs::registerAll(_translationService->registry());

        // Initialize signal values
        _throttlePercent = std::nullopt;
        _speedKmh = std::nullopt;
        _accelerationG = std::nullopt;
        _brakePercent = std::nullopt;
        _motorRpm = std::nullopt;
        _motorTorqueNm = std::nullopt;
        _gearSelector = std::nullopt;
        _steeringAngleDeg = std::nullopt;

        _isDemoMode.store(false);
        _isConnected.store(false);
        _protocol = VehicleProtocol::OBD2;
    }
    return self;
}

- (void)dealloc {
    [self stopDemo];
    [self disconnect];
}

// MARK: - Demo Mode

- (void)startDemo {
    std::lock_guard<std::mutex> lock(_mutex);

    // Stop any BLE connection first
    if (_bleManager->isConnected()) {
        _bleManager->stopOBD2Polling();
        _bleManager->stopCANMonitor();
        _bleManager->disconnect();
    }

    _connectedDeviceName = nil;
    _connectedDeviceAddress = nil;
    _isDemoMode.store(true);
    _isConnected.store(false);
    _simulator->start();
}

- (void)stopDemo {
    std::lock_guard<std::mutex> lock(_mutex);
    _isDemoMode.store(false);
    _simulator->stop();
}

// MARK: - BLE Live Mode

- (NSArray<VehicleSimDevice*> *)scanForDevices:(NSTimeInterval)timeout {
    auto devices = _bleManager->scanForDevices(static_cast<int>(timeout));

    NSMutableArray<VehicleSimDevice*> *result = [NSMutableArray arrayWithCapacity:devices.size()];
    for (const auto& dev : devices) {
        VehicleSimDevice *objcDev = [[VehicleSimDevice alloc] init];
        objcDev.name = [NSString stringWithUTF8String:dev.name.c_str()];
        objcDev.address = [NSString stringWithUTF8String:dev.address.c_str()];
        objcDev.rssi = dev.rssi;
        [result addObject:objcDev];
    }

    return result;
}

- (BOOL)connectToDevice:(NSString *)address deviceName:(NSString *)deviceName vehicleType:(NSString *)vehicleType {
    std::lock_guard<std::mutex> lock(_mutex);

    // Stop demo mode first
    if (_isDemoMode.load()) {
        _simulator->stop();
        _isDemoMode.store(false);
    }

    // Disconnect any existing connection
    if (_bleManager->isConnected()) {
        _bleManager->stopOBD2Polling();
        _bleManager->stopCANMonitor();
        _bleManager->disconnect();
    }

    std::string vehicleTypeStr = [vehicleType UTF8String];

    // Get the vehicle config to determine protocol (OCP: no hard-coded type checking)
    const auto* config = _translationService->registry().getConfig(vehicleTypeStr);
    if (!config) {
        return NO;
    }

    // Determine protocol from config (not from hard-coded vehicle type names)
    _protocol = config->isCANProtocol ? VehicleProtocol::CAN : VehicleProtocol::OBD2;

    // Load vehicle config (use embedded DBC for iOS - no file path dependency)
    std::string dbcContent = getEmbeddedDBC(vehicleTypeStr);
    if (!_translationService->loadVehicleWithContent(vehicleTypeStr, _protocol, dbcContent)) {
        return NO;
    }

    // Connect to device
    std::string addressStr = [address UTF8String];
    if (!_bleManager->connect(addressStr)) {
        return NO;
    }

    _connectedDeviceAddress = address;
    _connectedDeviceName = deviceName;
    _isConnected.store(true);

    // Wait for write + notify characteristics (blocks until discovered or timeout)
    if (!_bleManager->waitForCharacteristics(10000)) {
        _bleManager->disconnect();
        _isConnected.store(false);
        return NO;
    }

    // Initialize ELM327 based on protocol
    if (_protocol == VehicleProtocol::CAN) {
        if (!_bleManager->initializeCANMonitor()) {
            _bleManager->disconnect();
            _isConnected.store(false);
            return NO;
        }
    } else {
        if (!_bleManager->initializeELM327()) {
            _bleManager->disconnect();
            _isConnected.store(false);
            return NO;
        }
    }

    // Set up data callback
    _bleManager->onDataReceived([self](const std::vector<uint8_t>& data) {
        auto signal = self->_translationService->processFrame(data);
        if (signal) {
            // Update signal values (thread-safe via mutex)
            {
                std::lock_guard<std::mutex> lock(self->_mutex);
                self->_throttlePercent = signal->getThrottlePercent();
                self->_speedKmh = signal->getSpeedKmh();
                self->_accelerationG = signal->getAccelerationG();
                self->_brakePercent = signal->getBrakePercent();
                self->_motorRpm = signal->getMotorRpm();
                self->_motorTorqueNm = signal->getMotorTorqueNm();
                self->_steeringAngleDeg = signal->getSteeringAngleDeg();
                self->_gearSelector = signal->getGearSelector();
            }
        }
    });

    // Start polling
    if (_protocol == VehicleProtocol::CAN) {
        _bleManager->startCANMonitor(200);
    } else {
        _bleManager->startOBD2Polling(200);
    }

    return YES;
}

- (void)disconnect {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_bleManager->isConnected()) {
        if (_protocol == VehicleProtocol::CAN) {
            _bleManager->stopCANMonitor();
        } else {
            _bleManager->stopOBD2Polling();
        }
        _bleManager->disconnect();
    }

    _isConnected.store(false);
    _connectedDeviceName = nil;
    _connectedDeviceAddress = nil;
}

// MARK: - Signal Values

- (NSNumber *)throttlePercent {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getThrottlePercent();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _throttlePercent.has_value() ? @(_throttlePercent.value()) : nil;
}

- (NSNumber *)speedKmh {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getSpeedKmh();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _speedKmh.has_value() ? @(_speedKmh.value()) : nil;
}

- (NSNumber *)accelerationG {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getAccelerationG();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _accelerationG.has_value() ? @(_accelerationG.value()) : nil;
}

- (NSNumber *)brakePercent {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getBrakePercent();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _brakePercent.has_value() ? @(_brakePercent.value()) : nil;
}

- (NSNumber *)motorRpm {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getMotorRpm();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _motorRpm.has_value() ? @(_motorRpm.value()) : nil;
}

- (NSNumber *)motorTorqueNm {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getMotorTorqueNm();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _motorTorqueNm.has_value() ? @(_motorTorqueNm.value()) : nil;
}

- (NSString *)gearSelector {
    if (_isDemoMode.load()) {
        const auto& gear = _simulator->getLatestSignal().getGearSelector();
        return gear.has_value() ? [NSString stringWithUTF8String:gear->c_str()] : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _gearSelector.has_value() ? [NSString stringWithUTF8String:_gearSelector->c_str()] : nil;
}

- (NSNumber *)steeringAngleDeg {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getSteeringAngleDeg();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _steeringAngleDeg.has_value() ? @(_steeringAngleDeg.value()) : nil;
}

// MARK: - State

- (BOOL)isConnected {
    return _isConnected.load();
}

- (BOOL)isDemoMode {
    return _isDemoMode.load();
}

- (BOOL)isBluetoothReady {
    return _bleManager != nullptr;
}

- (NSString *)connectedDeviceName {
    return _connectedDeviceName;
}

- (NSString *)connectedDeviceAddress {
    return _connectedDeviceAddress;
}

- (void)updateSimulator {
    _simulator->update();
}

- (BOOL)switchVehicleType:(NSString *)vehicleType {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_isConnected.load()) return NO;

    std::string vehicleTypeStr = [vehicleType UTF8String];

    // Get the vehicle config to determine protocol (OCP: no hard-coded type checking)
    const auto* config = _translationService->registry().getConfig(vehicleTypeStr);
    if (!config) {
        return NO;
    }

    // Determine protocol from config (not from hard-coded vehicle type names)
    _protocol = config->isCANProtocol ? VehicleProtocol::CAN : VehicleProtocol::OBD2;

    std::string dbcContent = getEmbeddedDBC(vehicleTypeStr);
    bool loaded = _translationService->loadVehicleWithContent(vehicleTypeStr, _protocol, dbcContent);

    // Reset detector when switching vehicles
    if (_bleManager->vehicleDetector()) {
        _bleManager->vehicleDetector()->reset();
    }

    return loaded ? YES : NO;
}

- (NSString *)detectionInfo {
    auto* detector = _bleManager ? _bleManager->vehicleDetector() : nullptr;
    if (!detector) return @"";
    auto result = detector->getResult();
    if (result.frameCount == 0) return @"";

    NSMutableString* info = [NSMutableString string];
    [info appendFormat:@"Frames: %d", result.frameCount];

    if (!result.observedCanIds.empty()) {
        [info appendString:@" | CAN IDs:"];
        for (uint16_t id : result.observedCanIds) {
            [info appendFormat:@" 0x%04X", id];
        }
    }

    if (result.hasSuggestion()) {
        const char* conf = "";
        switch (result.confidence) {
            case domain::DetectionConfidence::High: conf = "high"; break;
            case domain::DetectionConfidence::Medium: conf = "medium"; break;
            case domain::DetectionConfidence::Low: conf = "low"; break;
            default: conf = "none"; break;
        }
        [info appendFormat:@" | %@ (%@)",
            [NSString stringWithUTF8String:result.suggestedVehicleId.c_str()],
            [NSString stringWithUTF8String:conf]];
    }

    return info;
}

- (BOOL)isReceivingData {
    auto* detector = _bleManager ? _bleManager->vehicleDetector() : nullptr;
    return detector ? detector->isReceivingData() : NO;
}

- (int)bleNotificationCount {
    return _bleManager ? _bleManager->bleNotificationCount() : 0;
}

- (NSString *)lastRawHex {
    if (!_bleManager) return @"";
    return [NSString stringWithUTF8String:_bleManager->lastRawHex().c_str()];
}

@end
