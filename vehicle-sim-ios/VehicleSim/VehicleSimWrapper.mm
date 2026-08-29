#import "VehicleSimWrapper.h"
#include "vehicle-sim/VehicleSim.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/TcpSignalSource.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/BLESignalSource.h"
#include "vehicle-sim/domain/DemoSignalSource.h"
#include "vehicle-sim/presentation/VehicleSignalFormatter.h"
#include <memory>
#include <string>
#include <vector>

using namespace vehicle_sim;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::pipeline;

// MARK: - VehicleSimDevice Implementation

@implementation VehicleSimDevice
@end

// MARK: - VehicleSimWrapper Implementation

@interface VehicleSimWrapper () {
    std::unique_ptr<BLEManager> _bleManager;
    std::unique_ptr<DBCTranslationService> _translationService;
    std::unique_ptr<ISignalSource> _signalSource;

    // Vehicle protocol for current connection
    VehicleProtocol _protocol;

    // Connected device info
    NSString *_connectedDeviceName;
    NSString *_connectedDeviceAddress;
}

// Shared vehicle-activation step (see the method's comment in the
// implementation): looks up the config, derives the protocol, resolves the
// bundled DBC, and loads it into the translation service.
- (BOOL)activateVehicleType:(NSString *)vehicleType;

@end

@implementation VehicleSimWrapper

- (instancetype)initWithVehicleType:(nullable NSString *)vehicleType {
    self = [super init];
    if (self) {
        _bleManager = std::make_unique<BLEManager>();
        _translationService = std::make_unique<DBCTranslationService>();

        // Register default vehicle configs
        DefaultVehicleConfigs::registerAll(_translationService->registry());

        _protocol = VehicleProtocol::OBD2;
    }
    return self;
}

- (instancetype)init {
    return [self initWithVehicleType:nil];
}

- (void)dealloc {
    [self stop];
}

// MARK: - Connection Control

- (void)startDemo {
    [self stop];

    _protocol = VehicleProtocol::Simulation;
    _signalSource = std::make_unique<domain::DemoSignalSource>(100);
    _signalSource->start();

    // Mirror the TCP/BLE connect paths so demo mode also reports its device id.
    _connectedDeviceName = @"Demo";
    _connectedDeviceAddress = @"demo";
}

- (void)startBLE {
    [self stop];

    // Clear device info
    _connectedDeviceName = nil;
    _connectedDeviceAddress = nil;
}

- (void)stop {
    // The signal source owns its pipeline thread and transport; stopping it
    // joins the thread (TCPSignalSource::stop) / detaches the BLE callbacks.
    if (_signalSource) {
        _signalSource->stop();
        _signalSource.reset();
    }

    if (_bleManager && _bleManager->isConnected()) {
        if (_protocol == VehicleProtocol::CAN) {
            _bleManager->stopCANMonitor();
        } else {
            _bleManager->stopOBD2Polling();
        }
        _bleManager->disconnect();
    }

    _connectedDeviceName = nil;
    _connectedDeviceAddress = nil;
}

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

- (BOOL)connectToDevice:(NSString *)address deviceName:(NSString *)deviceName {
    return [self connectToDevice:address deviceName:deviceName vehicleType:@"generic"];
}

- (BOOL)connectToDevice:(NSString *)address deviceName:(NSString *)deviceName vehicleType:(NSString *)vehicleType {
    // Stop any existing signal source
    [self stop];

    // If address starts with "tcp:" treat as TCP connection to ESP32
    NSString *prefix = @"tcp:";
    if ([address hasPrefix:prefix]) {
        return [self connectTCP:address deviceName:deviceName vehicleType:vehicleType];
    }

    std::string addressStr = [address UTF8String];
    if (!_bleManager->connect(addressStr)) {
        return NO;
    }

    _connectedDeviceAddress = address;
    _connectedDeviceName = deviceName;

    // Load vehicle type for translation if specified
    if (vehicleType && vehicleType.length > 0) {
        [self switchVehicleType:vehicleType];
    }

    // Wait for write + notify characteristics (blocks until discovered or timeout)
    if (!_bleManager->waitForCharacteristics(10000)) {
        _bleManager->disconnect();
        _connectedDeviceName = nil;
        _connectedDeviceAddress = nil;
        return NO;
    }

    // Initialize ELM327 (default to CAN for now, will be configurable later)
    if (!_bleManager->initializeCANMonitor()) {
        _bleManager->disconnect();
        _connectedDeviceName = nil;
        _connectedDeviceAddress = nil;
        return NO;
    }

    _protocol = VehicleProtocol::CAN;

    // Create BLE signal source
    // Note: BLESignalSource holds a reference to BLEManager (wrapper owns it)
    // and installs its own data callback in start().
    _signalSource = std::make_unique<BLESignalSource>(_bleManager.get());

    // Start the signal source
    _signalSource->start();

    // Start CAN monitoring
    _bleManager->startCANMonitor(200);

    return YES;
}

// MARK: - TCP Connection

/**
 * Shared vehicle-activation step for connectTCP and switchVehicleType.
 *
 * Looks up the vehicle config, derives the protocol (CAN vs OBD2), resolves
 * the DBC bundle file from the app bundle, and loads it into the translation
 * service. This is the ONLY place the wrapper activates a vehicle, so the
 * two entry points cannot drift apart. The protocol derivation and DBC
 * loading themselves are vanilla domain logic (DBCTranslationService); what
 * stays here is the NSBundle path lookup, which only exists on iOS.
 */
- (BOOL)activateVehicleType:(NSString *)vehicleType {
    std::string vt = [vehicleType UTF8String];
    const auto* config = _translationService->registry().getConfig(vt);
    if (!config) {
        return NO;
    }

    // Determine protocol from config
    _protocol = config->isCANProtocol ? VehicleProtocol::CAN : VehicleProtocol::OBD2;

    bool loaded = false;

    if (config->isCANProtocol && !config->dbcBundleFileName.empty()) {
        // Resolve DBC file path from the app bundle
        NSString *nsFileName = [NSString stringWithUTF8String:config->dbcBundleFileName.c_str()];
        NSString *bundlePath = [[NSBundle mainBundle] pathForResource:nsFileName ofType:nil];
        if (bundlePath) {
            std::string absPath = std::string([bundlePath UTF8String]);
            loaded = _translationService->loadVehicleFromPath(vt, _protocol, absPath);
        }
    } else {
        // OBD2 or no DBC needed
        loaded = _translationService->loadVehicleFromPath(vt, _protocol, "");
    }

    return loaded ? YES : NO;
}

/**
 * Establish a TCP connection to an ESP32 CAN bridge.
 *
 * Parses the tcp:<host>[:<port>] address (the canonical vanilla parser in
 * PipelineFactory — the same one the CLI uses), loads the vehicle DBC,
 * creates a TCPTransport + LiveTwaiSource pipeline, and starts a
 * TCPSignalSource that feeds decoded VehicleSignal frames to the wrapper's
 * polling interface.
 */
- (BOOL)connectTCP:(NSString *)address deviceName:(NSString *)deviceName vehicleType:(NSString *)vehicleType {
    std::string target = [address UTF8String];
    std::string host;
    int port = 3333;
    if (!parseTcpTarget(target, host, port)) {
        NSLog(@"[VehicleSimWrapper] Invalid TCP target: %@", address);
        return NO;
    }

    // Load vehicle DBC before starting the pipeline
    if (vehicleType && vehicleType.length > 0) {
        if (![self activateVehicleType:vehicleType]) {
            NSLog(@"[VehicleSimWrapper] Unknown or unloadable vehicle type: %@", vehicleType);
            return NO;
        }
    } else {
        // Default to generic OBD2
        _translationService->loadVehicleFromPath("generic", VehicleProtocol::OBD2, "");
        _protocol = VehicleProtocol::OBD2;
    }

    // Create the TCP transport. The StopToken is shared between the transport
    // and the signal source so stop() flips the flag the transport's hot loop
    // polls. Frame tokenisation happens in LiveTwaiSource (the canonical
    // seam) inside TCPSignalSource.
    auto stop = std::make_shared<StopToken>();
    auto transport = std::make_unique<TCPTransport>(
        TransportEndpoint{host, static_cast<int>(port), "raw"},
        std::make_shared<StdOut>(), TcpReadTiming{}, stop);

    // Open the transport to verify connectivity before starting the thread
    stop->reset();
    if (!transport->open()) {
        NSLog(@"[VehicleSimWrapper] Failed to open TCP transport to %s:%d", host.c_str(), port);
        return NO;
    }

    // Create the TCP signal source (takes ownership of the transport)
    auto tcpSource = std::make_unique<TCPSignalSource>(
        std::move(transport), *_translationService, stop);

    _signalSource = std::move(tcpSource);
    _signalSource->start();

    _connectedDeviceAddress = address;
    _connectedDeviceName = deviceName;

    NSLog(@"[VehicleSimWrapper] TCP connected to %s:%d (%@)", host.c_str(), port, vehicleType);
    return YES;
}

- (void)disconnect {
    [self stop];
}

- (NSArray<NSDictionary<NSString*, NSString*>*> *)getVehicleOptions {
    auto options = _translationService->registry().getVehicleOptions();

    NSMutableArray<NSDictionary<NSString*, NSString*>*> *result =
        [NSMutableArray arrayWithCapacity:options.size()];

    for (const auto& option : options) {
        NSDictionary *dict = @{
            @"id": [NSString stringWithUTF8String:option.id.c_str()],
            @"displayName": [NSString stringWithUTF8String:option.displayName.c_str()]
        };
        [result addObject:dict];
    }

    return result;
}

- (BOOL)switchVehicleType:(NSString *)vehicleType {
    if (![self activateVehicleType:vehicleType]) {
        return NO;
    }

    // Reset detector when switching vehicles
    if (_bleManager && _bleManager->vehicleDetector()) {
        _bleManager->vehicleDetector()->reset();
    }

    return YES;
}

// MARK: - Signal Values

- (NSNumber *)throttlePercent {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getThrottlePercent();
    return val.has_value() ? @(val.value()) : nil;
}

- (NSNumber *)speedKmh {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getSpeedKmh();
    return val.has_value() ? @(val.value()) : nil;
}

- (NSNumber *)accelerationG {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getAccelerationG();
    return val.has_value() ? @(val.value()) : nil;
}

- (NSNumber *)brakePercent {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getBrakePercent();
    return val.has_value() ? @(val.value()) : nil;
}

- (NSNumber *)motorRpm {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getMotorRpm();
    return val.has_value() ? @(val.value()) : nil;
}

- (NSNumber *)motorTorqueNm {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getMotorTorqueNm();
    return val.has_value() ? @(val.value()) : nil;
}

- (NSString *)gearSelector {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& gear = signal.getGearSelector();
    if (!gear.has_value()) return nil;

    // Convert gear constant to label using Gear::label()
    const char* label = Gear::label(gear.value());
    return label ? [NSString stringWithUTF8String:label] : nil;
}

- (NSNumber *)steeringAngleDeg {
    if (!_signalSource) return nil;
    const auto& signal = _signalSource->latestSignal();
    const auto& val = signal.getSteeringAngleDeg();
    return val.has_value() ? @(val.value()) : nil;
}

// MARK: - State

- (ConnectionState)connectionState {
    // A signal source exists only after a successful connect (demo/BLE/TCP),
    // and stop() clears it — so "source present" IS "connected". The
    // per-transport liveness detail (silent TCP drops) is isConnectionAlive's
    // job, below.
    return _signalSource ? ConnectionStateConnected : ConnectionStateDisconnected;
}

- (BOOL)isConnectionAlive {
    // BLE: alive if the BLE manager reports a live connection.
    if (_bleManager && _bleManager->isConnected()) {
        return YES;
    }

    // TCP: downcast to TCPSignalSource (the vanilla pipeline class) and check
    // its running flag. When the transport exhausts (peer close, network
    // drop), the pipeline thread sets it false, which this method surfaces so
    // the ViewModel can detect a silent drop.
    if (auto* tcpSource = dynamic_cast<TCPSignalSource*>(_signalSource.get())) {
        return tcpSource->isRunning() ? YES : NO;
    }

    // Demo source: always alive while _signalSource exists.
    if (_signalSource) {
        return YES;
    }

    return NO;
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

- (NSString *)detectionInfo {
    auto* detector = _bleManager ? _bleManager->vehicleDetector() : nullptr;
    if (!detector) return @"";
    // Summary formatting (frames / CAN IDs / suggestion+confidence) is the
    // vanilla presentation::formatDetectionSummary — unit-tested in ctest.
    const std::string summary =
        presentation::formatDetectionSummary(detector->getResult());
    return [NSString stringWithUTF8String:summary.c_str()];
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
