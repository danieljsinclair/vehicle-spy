#import "VehicleSimWrapper.h"
#include "vehicle-sim/VehicleSim.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/domain/CaptureLog.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/Gear.h"
#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/domain/BLESignalSource.h"
#include "vehicle-sim/domain/DemoSignalSource.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <chrono>

using namespace vehicle_sim;
using namespace vehicle_sim::domain;
using namespace vehicle_sim::pipeline;

// MARK: - VehicleSimDevice Implementation

@implementation VehicleSimDevice
@end

// MARK: - TCPSignalSource

/**
 * ISignalSource implementation that drives a TCPTransport + RawFrameNormaliser
 * pipeline on a background thread. Each decoded VehicleSignal is stored as the
 * latest signal, which the Objective-C++ wrapper polls from the main thread.
 *
 * Thread-safety: latestSignal() is called from the main thread (UI polling);
 * the pipeline writes from the background thread. A mutex protects the signal.
 *
 * Lifecycle: start() launches the pipeline thread; stop() requests the transport
 * to cease and joins the thread. The transport's stop flag is set so nextLine()
 * returns nullopt at its next select() timeout, cleanly ending runReplay().
 */
class TCPSignalSource final : public ISignalSource {
public:
    TCPSignalSource(std::unique_ptr<ITransport> transport,
                    std::unique_ptr<IAdapterNormaliser> normaliser,
                    DBCTranslationService& translationService)
        : transport_(std::move(transport))
        , normaliser_(std::move(normaliser))
        , translationService_(translationService)
    {}

    ~TCPSignalSource() override {
        stop();
    }

    TCPSignalSource(const TCPSignalSource&) = delete;
    TCPSignalSource& operator=(const TCPSignalSource&) = delete;

    [[nodiscard]] VehicleSignal latestSignal() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return latestSignal_.value_or(VehicleSignal(0));
    }

    void start() override {
        if (running_.exchange(true)) {
            return; // already running
        }
        TCPTransport::resetStop();
        if (!transport_->open()) {
            running_ = false;
            return;
        }
        worker_ = std::thread([this]() {
            runPipeline();
        });
    }

    void stop() override {
        if (!running_.exchange(false)) {
            return;
        }
        // Request the transport to return nullopt at its next select() timeout.
        TCPTransport::requestStop();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    std::unique_ptr<ITransport> transport_;
    std::unique_ptr<IAdapterNormaliser> normaliser_;
    DBCTranslationService& translationService_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::optional<VehicleSignal> latestSignal_;
    mutable std::mutex mutex_;

    // Capture callback for runReplay: we need to intercept decoded signals.
    // Since runReplay doesn't support a signal callback, we use a different
    // approach: drive the pipeline manually in the worker thread.
    void runPipeline() {
        while (running_ && transport_->isOpen()) {
            auto line = transport_->nextLine();
            if (!line) {
                break;
            }
            auto result = normaliser_->normalise(*line);
            if (result.kind == NormaliserResultKind::Frame) {
                auto bytes = toTwaiFrame(result.frame);
                std::optional<std::uint64_t> ts = result.hasTimestamp
                    ? std::optional<std::uint64_t>(result.frame.timestampMs)
                    : std::nullopt;
                auto signal = translationService_.processFrame(bytes, ts);
                if (signal) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    latestSignal_ = signal;
                }
            }
        }
        running_ = false;
    }
};

// MARK: - VehicleSimWrapper Implementation

@interface VehicleSimWrapper () {
    std::unique_ptr<BLEManager> _bleManager;
    std::unique_ptr<DBCTranslationService> _translationService;
    std::unique_ptr<ISignalSource> _signalSource;
    std::unique_ptr<pipeline::ITransport> _liveTransport;
    std::unique_ptr<pipeline::IAdapterNormaliser> _liveNormaliser;
    std::unique_ptr<std::thread> _liveWorker;
    bool _liveRunning;
    bool _connectionActive;
    std::optional<domain::VehicleSignal> _latestSignal;
    std::chrono::steady_clock::time_point _latestSignalTime;

    // Vehicle protocol for current connection
    VehicleProtocol _protocol;

    // Connected device info
    NSString *_connectedDeviceName;
    NSString *_connectedDeviceAddress;
}

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
}

- (void)startBLE {
    [self stop];

    // Clear device info
    _connectedDeviceName = nil;
    _connectedDeviceAddress = nil;
}

- (void)stop {
    _liveRunning = false;
    if (_liveWorker && _liveWorker->joinable()) {
        _liveWorker->join();
        _liveWorker.reset();
    }
    _liveTransport.reset();
    _liveNormaliser.reset();

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
    _signalSource = std::make_unique<BLESignalSource>(_bleManager.get());

    // Set up data callback for translation
    _bleManager->onDataReceived([self](const std::vector<uint8_t>& data) {
        // Translation will be handled by BLESignalSource
    });

    // Start the signal source
    _signalSource->start();

    // Start CAN monitoring
    _bleManager->startCANMonitor(200);

    return YES;
}

// MARK: - TCP Connection

/**
 * Parse a tcp:<host>:<port> or tcp:<host> address string.
 * Port defaults to 3333 (the ESP32 firmware default) when omitted.
 * Returns YES on success, NO if the format is invalid.
 */
- (BOOL)parseTcpTarget:(NSString *)address host:(std::string&)host port:(int&)port {
    std::string target = [address UTF8String];
    const std::string prefix = "tcp:";
    if (target.substr(0, prefix.size()) != prefix) {
        return NO;
    }

    std::string body = target.substr(prefix.size());
    if (body.empty()) {
        return NO;
    }

    // Split on the last ':' to separate host and port
    auto lastColon = body.rfind(':');
    if (lastColon != std::string::npos) {
        std::string hostPart = body.substr(0, lastColon);
        std::string portPart = body.substr(lastColon + 1);

        if (!portPart.empty() &&
            std::all_of(portPart.begin(), portPart.end(),
                        [](unsigned char c) { return std::isdigit(c); })) {
            try {
                int p = std::stoi(portPart);
                if (p < 1 || p > 65535) return NO;
                if (hostPart.empty()) return NO;
                host = hostPart;
                port = p;
                return YES;
            } catch (...) {
                return NO;
            }
        }
    }

    // No usable port token: whole body is the host, default port
    host = body;
    port = 3333;
    return YES;
}

/**
 * Establish a TCP connection to an ESP32 CAN bridge.
 *
 * Parses the tcp:<host>[:<port>] address, loads the vehicle DBC, creates a
 * TCPTransport + RawFrameNormaliser pipeline, and starts a TCPSignalSource
 * that feeds decoded VehicleSignal frames to the wrapper's polling interface.
 */
- (BOOL)connectTCP:(NSString *)address deviceName:(NSString *)deviceName vehicleType:(NSString *)vehicleType {
    std::string host;
    int port = 3333;
    if (![self parseTcpTarget:address host:host port:port]) {
        NSLog(@"[VehicleSimWrapper] Invalid TCP target: %@", address);
        return NO;
    }

    // Load vehicle DBC before starting the pipeline
    if (vehicleType && vehicleType.length > 0) {
        std::string vt = [vehicleType UTF8String];
        const auto* config = _translationService->registry().getConfig(vt);
        if (!config) {
            NSLog(@"[VehicleSimWrapper] Unknown vehicle type: %@", vehicleType);
            return NO;
        }
        _protocol = config->isCANProtocol ? VehicleProtocol::CAN : VehicleProtocol::OBD2;

        bool loaded = false;
        if (config->isCANProtocol && !config->dbcBundleFileName.empty()) {
            NSString *nsFileName = [NSString stringWithUTF8String:config->dbcBundleFileName.c_str()];
            NSString *bundlePath = [[NSBundle mainBundle] pathForResource:nsFileName ofType:nil];
            if (bundlePath) {
                std::string absPath = std::string([bundlePath UTF8String]);
                loaded = _translationService->loadVehicleFromPath(vt, _protocol, absPath);
            }
        } else {
            loaded = _translationService->loadVehicleFromPath(vt, _protocol, "");
        }

        if (!loaded) {
            NSLog(@"[VehicleSimWrapper] Failed to load vehicle DBC: %@", vehicleType);
            return NO;
        }
    } else {
        // Default to generic OBD2
        _translationService->loadVehicleFromPath("generic", VehicleProtocol::OBD2, "");
        _protocol = VehicleProtocol::OBD2;
    }

    // Create TCP transport and raw frame normaliser
    auto transport = std::make_unique<TCPTransport>(host, port, "raw");
    auto normaliser = std::make_unique<RawFrameNormaliser>();

    // Open the transport to verify connectivity before starting the thread
    TCPTransport::resetStop();
    if (!transport->open()) {
        NSLog(@"[VehicleSimWrapper] Failed to open TCP transport to %s:%d", host.c_str(), port);
        return NO;
    }

    // Create the TCP signal source (takes ownership of transport + normaliser)
    auto tcpSource = std::make_unique<TCPSignalSource>(
        std::move(transport), std::move(normaliser), *_translationService);

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
    std::string vehicleTypeStr = [vehicleType UTF8String];

    // Get the vehicle config
    const auto* config = _translationService->registry().getConfig(vehicleTypeStr);
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
            loaded = _translationService->loadVehicleFromPath(vehicleTypeStr, _protocol, absPath);
        }
    } else {
        // OBD2 or no DBC needed
        loaded = _translationService->loadVehicleFromPath(vehicleTypeStr, _protocol, "");
    }

    // Reset detector when switching vehicles
    if (_bleManager && _bleManager->vehicleDetector()) {
        _bleManager->vehicleDetector()->reset();
    }

    return loaded ? YES : NO;
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
    if (!_signalSource) {
        return ConnectionStateDisconnected;
    }

    // For TCP: check if the source is still running
    // For BLE: check BLE connection status
    if (_bleManager && _bleManager->isConnected()) {
        return ConnectionStateConnected;
    }

    // TCP sources are considered connected if they have been started
    // (the signal source exists and was started)
    if (_signalSource) {
        return ConnectionStateConnected;
    }

    return ConnectionStateConnecting;
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
