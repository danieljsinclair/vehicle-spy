#include "vehicle-sim/ble/platform/BLEManageriOS.h"
#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <dispatch/dispatch.h>

// Forward declaration for C++ class used in ObjC delegate
namespace vehicle_sim { class BLEManageriOS; }

// MARK: - Objective-C Delegate Declaration (MUST be at global scope)
@interface BLEManageriOSDelegate : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property (nonatomic, assign) vehicle_sim::BLEManageriOS* manager;
@end

@implementation BLEManageriOSDelegate

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    if (central.state == CBManagerStatePoweredOn) {
        std::cout << "[BLEManageriOS] Bluetooth powered on, ready to scan" << std::endl;
        if (self.manager) {
            self.manager->onBluetoothStateChanged(true);
        }
    } else if (central.state == CBManagerStatePoweredOff) {
        std::cout << "[BLEManageriOS] Bluetooth powered off" << std::endl;
        if (self.manager) {
            self.manager->onBluetoothStateChanged(false);
        }
    } else if (central.state == CBManagerStateUnauthorized) {
        std::cerr << "[BLEManageriOS] Bluetooth unauthorized - check permissions in Settings" << std::endl;
    } else if (central.state == CBManagerStateUnsupported) {
        std::cerr << "[BLEManageriOS] Bluetooth not supported on this device" << std::endl;
    }
}

- (void)centralManager:(CBCentralManager *)central
didDiscoverPeripheral:(CBPeripheral *)peripheral
    advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                 RSSI:(NSNumber *)RSSI {
    if (self.manager && peripheral) {
        vehicle_sim::BLEDeviceInfo device;

        if (peripheral.name) {
            device.name = std::string([peripheral.name UTF8String]);
        } else {
            device.name = "Unknown OBD2 Device";
        }

        device.address = std::string([[peripheral.identifier UUIDString] UTF8String]);
        device.isConnected = false;
        device.rssi = [RSSI intValue];

        // Store peripheral for later connection (retained via manual retain)
        device.peripheral = (void*)CFBridgingRetain(peripheral);

        // Use public wrapper to add device (handles deduplication + callbacks)
        self.manager->addDevice(device);
    }
}

- (void)centralManager:(CBCentralManager *)central
didConnectPeripheral:(CBPeripheral *)peripheral {
    if (peripheral) {
        std::cout << "[BLEManageriOS] Connected to: "
                  << (peripheral.name ? [peripheral.name UTF8String] : "unknown") << std::endl;
        peripheral.delegate = self;

        // Discover all services
        [peripheral discoverServices:nil];

        if (self.manager) {
            std::string deviceId = std::string([[peripheral.identifier UUIDString] UTF8String]);
            self.manager->onConnectionStateChanged(true, deviceId);
        }
    }
}

- (void)centralManager:(CBCentralManager *)central
didDisconnectPeripheral:(CBPeripheral *)peripheral
                 error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManageriOS] Disconnected with error: "
                  << [error.localizedDescription UTF8String] << std::endl;
    } else {
        std::cout << "[BLEManageriOS] Disconnected cleanly" << std::endl;
    }

    if (self.manager) {
        self.manager->onConnectionStateChanged(false, "");
    }
}

- (void)centralManager:(CBCentralManager *)central
didFailToConnectPeripheral:(CBPeripheral *)peripheral
                 error:(NSError *)error {
    std::cerr << "[BLEManageriOS] Failed to connect: "
              << (error ? [error.localizedDescription UTF8String] : "unknown error") << std::endl;

    if (self.manager) {
        self.manager->onConnectionStateChanged(false, "");
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManageriOS] Service discovery error: "
                  << [error.localizedDescription UTF8String] << std::endl;
        return;
    }

    if (peripheral && peripheral.services) {
        std::cout << "[BLEManageriOS] Discovered " << peripheral.services.count << " services" << std::endl;

        for (CBService* service in peripheral.services) {
            std::cout << "[BLEManageriOS] Service: " << [service.UUID UUIDString] << std::endl;
            [peripheral discoverCharacteristics:nil forService:service];
        }
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didDiscoverCharacteristicsForService:(CBService *)service
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManageriOS] Characteristic discovery error: "
                  << [error.localizedDescription UTF8String] << std::endl;
        return;
    }

    if (service && service.characteristics) {
        for (CBCharacteristic* characteristic in service.characteristics) {
            std::cout << "[BLEManageriOS] Characteristic: "
                      << [characteristic.UUID UUIDString]
                      << " ("
                      << (characteristic.properties & CBCharacteristicPropertyNotify ? "notify" : "")
                      << (characteristic.properties & CBCharacteristicPropertyWrite ? "write" : "")
                      << ")"
                      << std::endl;

            // Subscribe to notifications if available
            if (characteristic.properties & CBCharacteristicPropertyNotify) {
                [peripheral setNotifyValue:YES forCharacteristic:characteristic];
            }
        }
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManageriOS] Notification setup error: "
                  << [error.localizedDescription UTF8String] << std::endl;
    } else {
        std::cout << "[BLEManageriOS] Notifications enabled for: "
                  << [characteristic.UUID UUIDString] << std::endl;
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManageriOS] Read error: "
                  << [error.localizedDescription UTF8String] << std::endl;
        return;
    }

    if (characteristic.value && self.manager) {
        const uint8_t* bytes = (const uint8_t*)characteristic.value.bytes;
        NSUInteger length = characteristic.value.length;
        std::vector<uint8_t> data(bytes, bytes + length);

        // Use C++ method to invoke data callback with OBD2 parsing support
        self.manager->onDataReceived(data);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didWriteValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManageriOS] Write error: "
                  << [error.localizedDescription UTF8String] << std::endl;
    } else {
        std::cout << "[BLEManageriOS] Write successful to: "
                  << [characteristic.UUID UUIDString] << std::endl;
    }
}

@end  // End of BLEManageriOSDelegate

// Now enter C++ namespace for the implementation
namespace vehicle_sim {

// MARK: - C++ Implementation

BLEManageriOS::BLEManageriOS()
    : BLEManagerBase()
    , connected_(false)
{
    BLEManageriOSDelegate* delegate = [[BLEManageriOSDelegate alloc] init];
    delegate.manager = this;

    // Store delegate to prevent deallocation (manual retain since no ARC)
    delegate_ = (void*)CFBridgingRetain(delegate);
    [delegate release];

    // Create dispatch queue for BLE operations
    dispatch_queue_t bleQueue = dispatch_queue_create("com.vehicle-sim.ble.ios", DISPATCH_QUEUE_SERIAL);

    // Initialize central manager with delegate
    central_manager_ = [[CBCentralManager alloc] initWithDelegate:delegate queue:bleQueue options:nil];
}

BLEManageriOS::~BLEManageriOS() {
    if (connected_peripheral_) {
        [central_manager_ cancelPeripheralConnection:connected_peripheral_];
        [connected_peripheral_ release];
        connected_peripheral_ = nullptr;
    }

    if (central_manager_) {
        [central_manager_ stopScan];
        [central_manager_ release];
        central_manager_ = nullptr;
    }

    // Release delegate
    if (delegate_) {
        CFRelease(delegate_);
        delegate_ = nullptr;
    }
}

std::vector<BLEDeviceInfo> BLEManageriOS::scanForDevices(int timeout_seconds) {
    std::cout << "[BLEManageriOS] Starting BLE scan for " << timeout_seconds << " seconds..." << std::endl;

    if (!central_manager_) {
        std::cerr << "[BLEManageriOS] Central manager not initialized" << std::endl;
        return {};
    }

    // Wait for Bluetooth to be ready
    if (!waitForBluetoothReady(1000)) {
        std::cerr << "[BLEManageriOS] Bluetooth not ready" << std::endl;
        return {};
    }

    // Clear previous discoveries (use base class method)
    clearDiscoveredDevices();

    // Scan for all peripherals (nil = no service filter)
    [central_manager_ scanForPeripheralsWithServices:nil
                                               options:@{CBCentralManagerScanOptionAllowDuplicatesKey: @NO}];

    std::cout << "[BLEManageriOS] Scanning..." << std::endl;

    // Wait for timeout
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count() < timeout_seconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    [central_manager_ stopScan];

    std::cout << "[BLEManageriOS] Scan complete. Found " << discovered_devices_.size() << " device(s)" << std::endl;

    return discovered_devices_;
}

bool BLEManageriOS::connect(const std::string& device_identifier) {
    std::cout << "[BLEManageriOS] Attempting to connect to: " << device_identifier << std::endl;

    if (!central_manager_) {
        std::cerr << "[BLEManageriOS] Central manager not initialized" << std::endl;
        return false;
    }

    // Find the peripheral from our discovered list using base class method
    auto device = findDeviceByAddress(device_identifier);
    CBPeripheral* target_peripheral = nullptr;

    if (device && device->peripheral) {
        // Transfer ownership from void* to CBPeripheral*
        target_peripheral = (__bridge CBPeripheral*)device->peripheral;
        // Clear the void* to prevent double-free
        const_cast<BLEDeviceInfo&>(*device).peripheral = nullptr;
    } else {
        // Try to retrieve by UUID if not in discovered list
        NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:[NSString stringWithUTF8String:device_identifier.c_str()]];
        if (uuid) {
            NSArray* peripherals = [central_manager_ retrievePeripheralsWithIdentifiers:@[uuid]];
            if (peripherals.count > 0) {
                target_peripheral = peripherals.firstObject;
            }
        }

        if (!target_peripheral) {
            std::cerr << "[BLEManageriOS] Device not found: " << device_identifier << std::endl;
            return false;
        }
    }

    // Store peripheral reference
    connected_peripheral_ = target_peripheral;
    [connected_peripheral_ retain];

    // Connect
    [central_manager_ connectPeripheral:connected_peripheral_ options:nil];

    // Connection is async - report success if no immediate error
    connected_ = true;
    connected_device_id_ = device_identifier;

    // Update base class state
    setConnectionState(true, device_identifier);

    std::cout << "[BLEManageriOS] Connection initiated..." << std::endl;
    return true;
}

void BLEManageriOS::disconnect() {
    std::cout << "[BLEManageriOS] Disconnecting..." << std::endl;

    if (connected_peripheral_) {
        [central_manager_ cancelPeripheralConnection:connected_peripheral_];
        [connected_peripheral_ release];
        connected_peripheral_ = nullptr;
    }

    connected_ = false;
    connected_device_id_.clear();

    // Update base class state
    setConnectionState(false, "");

    std::cout << "[BLEManageriOS] Disconnected" << std::endl;
}

void BLEManageriOS::send(const std::vector<uint8_t>& data) {
    if (!connected_peripheral_) {
        std::cerr << "[BLEManageriOS] Not connected" << std::endl;
        return;
    }

    NSData* nsData = [NSData dataWithBytes:data.data() length:data.size()];
    // Send to first writable characteristic discovered
    for (CBService* service in connected_peripheral_.services) {
        for (CBCharacteristic* characteristic in service.characteristics) {
            if (characteristic.properties & CBCharacteristicPropertyWrite) {
                [connected_peripheral_ writeValue:nsData
                                forCharacteristic:characteristic
                                             type:CBCharacteristicWriteWithResponse];
                return;
            }
        }
    }

    std::cerr << "[BLEManageriOS] No writable characteristic found" << std::endl;
}

bool BLEManageriOS::isConnected() const {
    return connected_;
}

std::string BLEManageriOS::getConnectedDeviceId() const {
    return connected_device_id_;
}

int BLEManageriOS::getBluetoothState() const {
    return static_cast<int>(central_manager_.state);
}

bool BLEManageriOS::isBluetoothReady() const {
    return central_manager_ && central_manager_.state == CBManagerStatePoweredOn;
}

bool BLEManageriOS::initializeELM327() {
    // Use base class implementation
    return BLEManagerBase::initializeELM327();
}

// MARK: - Private Callback Methods (Platform-specific)

void BLEManageriOS::onDeviceDiscovered(const BLEDeviceInfo& device) {
    // Device addition is now handled by base class addDiscoveredDevice()
    // This callback can be used for additional platform-specific handling
    std::cout << "[BLEManageriOS] Device discovered callback: " << device.name << std::endl;
}

void BLEManageriOS::onCharacteristicNotification(const std::vector<uint8_t>& data) {
    // Use base class method for callback invocation (includes OBD2 parsing)
    invokeDataCallback(data);
}

void BLEManageriOS::onDataReceived(const std::vector<uint8_t>& data) {
    invokeDataCallback(data);
}

void BLEManageriOS::onConnectionStateChanged(bool is_connected, const std::string& device_id) {
    connected_ = is_connected;
    if (is_connected && !device_id.empty()) {
        connected_device_id_ = device_id;
        std::cout << "[BLEManageriOS] Connection established: " << device_id << std::endl;
    } else {
        connected_device_id_.clear();
        std::cout << "[BLEManageriOS] Connection lost" << std::endl;
    }

    // Update base class state
    setConnectionState(is_connected, device_id);
}

void BLEManageriOS::onServicesDiscovered() {
    std::cout << "[BLEManageriOS] Services discovered" << std::endl;
}

void BLEManageriOS::onCharacteristicsDiscovered() {
    std::cout << "[BLEManageriOS] Characteristics discovered" << std::endl;
}

void BLEManageriOS::onBluetoothStateChanged(bool isPoweredOn) {
    if (!isPoweredOn) {
        connected_ = false;
        setConnectionState(false, "");
        std::cout << "[BLEManageriOS] Bluetooth became unavailable" << std::endl;
    }
}

// MARK: - Private Helper Methods (Platform-specific)

bool BLEManageriOS::waitForBluetoothReady(int timeout_ms) {
    if (!central_manager_) return false;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    if (central_manager_.state == CBManagerStatePoweredOn) {
        return true;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        usleep(100 * 1000);
        if (central_manager_.state == CBManagerStatePoweredOn ||
            central_manager_.state == CBManagerStateUnauthorized) {
            dispatch_semaphore_signal(sem);
            return;
        }
        usleep(400 * 1000);
        dispatch_semaphore_signal(sem);
    });

    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_ms * NSEC_PER_MSEC)));

    if (central_manager_.state != CBManagerStatePoweredOn) {
        std::cerr << "[BLEManageriOS] Bluetooth not ready. State: " << central_manager_.state << std::endl;
        return false;
    }
    return true;
}

CBPeripheral* BLEManageriOS::findPeripheralByAddress(const std::string& address) {
    auto device = findDeviceByAddress(address);
    if (device && device->peripheral) {
        CBPeripheral* peripheral = (__bridge CBPeripheral*)device->peripheral;
        const_cast<BLEDeviceInfo&>(*device).peripheral = nullptr;
        return peripheral;
    }
    return nullptr;
}

} // namespace vehicle_sim