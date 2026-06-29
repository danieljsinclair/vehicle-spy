#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

#include "vehicle-sim/ble/platform/BLEManagerMacOS.h"
#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <dispatch/dispatch.h>

// MARK: - Objective-C Delegate Declaration (MUST be at global scope)
@interface BLEMacOSDelegate : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property (nonatomic, assign) vehicle_sim::BLEManagerMacOS* manager;
@end

@implementation BLEMacOSDelegate

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    if (central.state == CBManagerStatePoweredOn) {
        std::cout << "[BLEManagerMacOS] Bluetooth powered on" << std::endl;
        if (self.manager) {
            self.manager->onBluetoothStateChanged(true);
        }
    } else if (central.state == CBManagerStatePoweredOff) {
        std::cout << "[BLEManagerMacOS] Bluetooth powered off" << std::endl;
        if (self.manager) {
            self.manager->onBluetoothStateChanged(false);
        }
    } else if (central.state == CBManagerStateUnauthorized) {
        std::cerr << "[BLEManagerMacOS] Bluetooth unauthorized - check permissions" << std::endl;
    } else if (central.state == CBManagerStateUnsupported) {
        std::cerr << "[BLEManagerMacOS] Bluetooth not supported on this device" << std::endl;
    }
}

- (void)centralManager:(CBCentralManager *)central
didDiscoverPeripheral:(CBPeripheral *)peripheral
    advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                 RSSI:(NSNumber *)RSSI {
    if (self.manager && peripheral) {
        // Create BLEDeviceInfo from discovered peripheral
        vehicle_sim::BLEDeviceInfo device;

        if (peripheral.name) {
            device.name = std::string([peripheral.name UTF8String]);
        } else {
            device.name = "Unknown Device";
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
        std::cout << "[BLEManagerMacOS] Connected to: "
                  << (peripheral.name ? [peripheral.name UTF8String] : "unknown") << std::endl;
        peripheral.delegate = self;
        [peripheral discoverServices:nil]; // Discover all services

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
        std::cerr << "[BLEManagerMacOS] Disconnected with error: "
                  << [error.localizedDescription UTF8String] << std::endl;
    } else {
        std::cout << "[BLEManagerMacOS] Disconnected cleanly" << std::endl;
    }

    if (self.manager) {
        self.manager->onConnectionStateChanged(false, "");
    }
}

- (void)centralManager:(CBCentralManager *)central
didFailToConnectPeripheral:(CBPeripheral *)peripheral
                 error:(NSError *)error {
    std::cerr << "[BLEManagerMacOS] Failed to connect: "
              << (error ? [error.localizedDescription UTF8String] : "unknown error") << std::endl;

    if (self.manager) {
        self.manager->onConnectionStateChanged(false, "");
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManagerMacOS] Service discovery error: "
                  << [error.localizedDescription UTF8String] << std::endl;
        return;
    }

    if (peripheral && peripheral.services) {
        std::cout << "[BLEManagerMacOS] Discovered " << peripheral.services.count << " services" << std::endl;

        for (CBService* service in peripheral.services) {
            std::cout << "[BLEManagerMacOS] Service: " << [service.UUID UUIDString] << std::endl;
            [peripheral discoverCharacteristics:nil forService:service];
        }
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didDiscoverCharacteristicsForService:(CBService *)service
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManagerMacOS] Characteristic discovery error: "
                  << [error.localizedDescription UTF8String] << std::endl;
        return;
    }

    if (service && service.characteristics) {
        for (CBCharacteristic* characteristic in service.characteristics) {
            std::cout << "[BLEManagerMacOS] Characteristic: "
                      << [characteristic.UUID UUIDString]
                      << " ("
                      << (characteristic.properties & CBCharacteristicPropertyNotify ? "notify" : "")
                      << (characteristic.properties & CBCharacteristicPropertyWrite ? "write" : "")
                      << ")"
                      << std::endl;

            if (self.manager) {
                self.manager->onCharacteristicDiscovered(characteristic);
            }

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
        std::cerr << "[BLEManagerMacOS] Notification setup error: "
                  << [error.localizedDescription UTF8String] << std::endl;
    } else {
        std::cout << "[BLEManagerMacOS] Notifications enabled for: "
                  << [characteristic.UUID UUIDString] << std::endl;
    }

    if (self.manager) {
        self.manager->onCharacteristicDiscovered(characteristic);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManagerMacOS] Read error: "
                  << [error.localizedDescription UTF8String] << std::endl;
        return;
    }

    if (characteristic.value && self.manager) {
        const uint8_t* bytes = (const uint8_t*)characteristic.value.bytes;
        NSUInteger length = characteristic.value.length;
        std::vector<uint8_t> data(bytes, bytes + length);

        // Use base class method to invoke data callback with OBD2 parsing support
        self.manager->onDataReceived(data);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral
didWriteValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error {
    if (error) {
        std::cerr << "[BLEManagerMacOS] Write error: "
                  << [error.localizedDescription UTF8String] << std::endl;
    } else {
        std::cout << "[BLEManagerMacOS] Write successful to: "
                  << [characteristic.UUID UUIDString] << std::endl;
    }
}

@end  // End of BLEMacOSDelegate

// Now enter C++ namespace for the implementation
namespace vehicle_sim {

// C++ Implementation starts here
BLEManagerMacOS::BLEManagerMacOS()
    : BLEManagerBase()  // Initialize base class
{
    // Create delegate and assign to member variable
    BLEMacOSDelegate* delegate = [[BLEMacOSDelegate alloc] init];
    delegate.manager = this;

    // Store delegate to prevent deallocation (manual retain since no ARC)
    delegate_ = (void*)CFBridgingRetain(delegate);
    [delegate release];

    // Create dispatch queue for BLE operations
    dispatch_queue_t bleQueue = dispatch_queue_create("com.vehicle-sim.ble", DISPATCH_QUEUE_SERIAL);

    // Initialize central manager with delegate
    central_manager_ = [[CBCentralManager alloc] initWithDelegate:delegate queue:bleQueue options:nil];
}

BLEManagerMacOS::~BLEManagerMacOS() {
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

std::vector<BLEDeviceInfo> BLEManagerMacOS::scanForDevices(int timeout_seconds) {
    std::cout << "[BLEManagerMacOS] Starting BLE scan for " << timeout_seconds << " seconds..." << std::endl;
    std::cout << "[BLEManagerMacOS] Ensure your OBD2 adapter is powered and in range." << std::endl;

    if (!central_manager_) {
        std::cerr << "[BLEManagerMacOS] Central manager not initialized" << std::endl;
        return {};
    }

    // Wait for Bluetooth to be ready
    if (!waitForBluetoothReady(1000)) {
        std::cerr << "[BLEManagerMacOS] Bluetooth not ready" << std::endl;
        return {};
    }

    // Clear previous discoveries (use base class method)
    clearDiscoveredDevices();

    // Scan for all peripherals (nil = no service filter)
    [central_manager_ scanForPeripheralsWithServices:nil
                                               options:@{CBCentralManagerScanOptionAllowDuplicatesKey: @NO}];

    std::cout << "[BLEManagerMacOS] Scanning..." << std::endl;

    // Wait for timeout
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count() < timeout_seconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    [central_manager_ stopScan];

    // Get devices from base class (which was populated by delegate callbacks)
    std::cout << "[BLEManagerMacOS] Scan complete. Found " << discovered_devices_.size() << " device(s)" << std::endl;

    return discovered_devices_;
}

bool BLEManagerMacOS::connect(std::string_view device_identifier) {
    std::cout << "[BLEManagerMacOS] Attempting to connect to: " << device_identifier << std::endl;

    if (!central_manager_) {
        std::cerr << "[BLEManagerMacOS] Central manager not initialized" << std::endl;
        return false;
    }

    // Wait for Bluetooth to be ready (connect may run in a new process without prior scan)
    if (!waitForBluetoothReady(3000)) {
        std::cerr << "[BLEManagerMacOS] Bluetooth not ready - check permissions" << std::endl;
        return false;
    }

    // Find the peripheral from our discovered list using base class method
    auto device = findDeviceByAddress(device_identifier);
    CBPeripheral* target_peripheral = nullptr;

    if (device && device->peripheral) {
        // Cast from void* to CBPeripheral* (stored via CFBridgingRetain, +1 retain)
        target_peripheral = (CBPeripheral*)device->peripheral;
        // Clear the void* to prevent double-free
        const_cast<BLEDeviceInfo&>(*device).peripheral = nullptr;
    } else {
        // Try to retrieve by UUID if not in discovered list
        std::string identifier_str(device_identifier);
        NSUUID* uuid = [[NSUUID alloc] initWithUUIDString:[NSString stringWithUTF8String:identifier_str.c_str()]];
        if (uuid) {
            NSArray* peripherals = [central_manager_ retrievePeripheralsWithIdentifiers:@[uuid]];
            if (peripherals.count > 0) {
                target_peripheral = peripherals.firstObject;
            }
        }

        if (!target_peripheral) {
            std::cerr << "[BLEManagerMacOS] Device not found: " << device_identifier << std::endl;
            std::cerr << "[BLEManagerMacOS] Run --scan first to discover available devices" << std::endl;
            return false;
        }
    }

    // Store peripheral reference
    connected_peripheral_ = target_peripheral;
    [connected_peripheral_ retain];

    // Connect
    [central_manager_ connectPeripheral:connected_peripheral_ options:nil];

    // Connection is async - report success if no immediate error
    // The delegate callbacks will confirm actual connection state
    connected_ = true;
    connected_device_id_ = std::string(device_identifier);

    // Update base class state
    setConnectionState(true, device_identifier);

    std::cout << "[BLEManagerMacOS] Connection initiated..." << std::endl;
    return true;
}

void BLEManagerMacOS::disconnect() {
    std::cout << "[BLEManagerMacOS] Disconnecting..." << std::endl;

    if (connected_peripheral_) {
        [central_manager_ cancelPeripheralConnection:connected_peripheral_];
        [connected_peripheral_ release];
        connected_peripheral_ = nullptr;
    }

    write_characteristic_ = nullptr;
    notify_characteristic_ = nullptr;

    connected_ = false;
    connected_device_id_.clear();

    // Update base class state
    setConnectionState(false, "");

    std::cout << "[BLEManagerMacOS] Disconnected" << std::endl;
}

void BLEManagerMacOS::send(const std::vector<uint8_t>& data) {
    if (!connected_peripheral_ || !write_characteristic_) {
        std::cerr << "[BLEManagerMacOS] Not connected or no write characteristic" << std::endl;
        return;
    }

    NSData* nsData = [NSData dataWithBytes:data.data() length:data.size()];
    [connected_peripheral_ writeValue:nsData
                    forCharacteristic:write_characteristic_
                                 type:CBCharacteristicWriteWithResponse];

    std::cout << "[BLEManagerMacOS] Sent " << data.size() << " bytes" << std::endl;
}

bool BLEManagerMacOS::isConnected() const {
    return connected_;
}

std::string BLEManagerMacOS::getConnectedDeviceId() const {
    return connected_device_id_;
}

int BLEManagerMacOS::getBluetoothState() const {
    return static_cast<int>(central_manager_.state);
}

bool BLEManagerMacOS::isBluetoothReady() const {
    return central_manager_ && central_manager_.state == CBManagerStatePoweredOn;
}

// MARK: - Private Callback Methods (Platform-specific)

void BLEManagerMacOS::onDeviceDiscovered(const BLEDeviceInfo& device) {
    // Device addition is now handled by base class addDiscoveredDevice()
    // This callback can be used for additional platform-specific handling
    std::cout << "[BLEManagerMacOS] Device discovered callback: " << device.name << std::endl;
}

void BLEManagerMacOS::onDataReceived(const std::vector<uint8_t>& data) {
    // Use base class method for callback invocation (includes OBD2 parsing)
    invokeDataCallback(data);
}

void BLEManagerMacOS::onConnectionStateChanged(bool is_connected, const std::string& device_id) {
    connected_ = is_connected;
    if (is_connected && !device_id.empty()) {
        connected_device_id_ = device_id;
        std::cout << "[BLEManagerMacOS] Connection established: " << device_id << std::endl;
    } else {
        connected_device_id_.clear();
        std::cout << "[BLEManagerMacOS] Connection lost" << std::endl;
    }

    // Update base class state
    setConnectionState(is_connected, device_id);
}

void BLEManagerMacOS::onCharacteristicDiscovered(CBCharacteristic* characteristic) {
    bool gotWrite = false;
    bool gotNotify = false;

    if (characteristic.properties & CBCharacteristicPropertyWrite) {
        write_characteristic_ = characteristic;
        [write_characteristic_ retain];
        std::cout << "[BLEManagerMacOS] Write characteristic found: "
                  << [characteristic.UUID UUIDString] << std::endl;
        gotWrite = true;
    }

    if (characteristic.properties & CBCharacteristicPropertyNotify) {
        notify_characteristic_ = characteristic;
        [notify_characteristic_ retain];
        std::cout << "[BLEManagerMacOS] Notify characteristic found: "
                  << [characteristic.UUID UUIDString] << std::endl;
        gotNotify = true;
    }

    if (gotWrite || gotNotify) {
        std::scoped_lock lock(characteristics_mutex_);
        characteristics_cv_.notify_all();
    }
}

void BLEManagerMacOS::onBluetoothStateChanged(bool isPoweredOn) {
    if (!isPoweredOn) {
        connected_ = false;
        setConnectionState(false, "");
        std::cout << "[BLEManagerMacOS] Bluetooth became unavailable" << std::endl;
    }
}

// MARK: - Private Helper Methods (Platform-specific)

bool BLEManagerMacOS::waitForBluetoothReady(int timeout_ms) {
    if (!central_manager_) return false;

    // Use dispatch semaphore so the delegate callback on the serial queue
    // can signal us when Bluetooth powers on
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    if (central_manager_.state == CBManagerStatePoweredOn) {
        return true; // Already ready
    }

    // Wait on the serial queue (same queue as BLE delegate callbacks)
    dispatch_async(dispatch_get_main_queue(), ^{
        // Check if powered on
        if (central_manager_.state == CBManagerStatePoweredOn) {
            dispatch_semaphore_signal(sem);
            return;
        }
        // Otherwise wait and check again
        usleep(100 * 1000); // 100ms initial wait
        if (central_manager_.state == CBManagerStatePoweredOn ||
            central_manager_.state == CBManagerStateUnauthorized) {
            dispatch_semaphore_signal(sem);
            return;
        }
        usleep(400 * 1000); // 400ms more wait
        dispatch_semaphore_signal(sem);
    });

    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_ms * NSEC_PER_MSEC)));

    if (central_manager_.state != CBManagerStatePoweredOn) {
        std::cerr << "[BLEManagerMacOS] Bluetooth not ready. State: " << central_manager_.state << std::endl;
        return false;
    }
    return true;
}

CBPeripheral* BLEManagerMacOS::findPeripheralByAddress(const std::string& address) {
    auto device = findDeviceByAddress(address);
    if (device && device->peripheral) {
        CBPeripheral* peripheral = (CBPeripheral*)device->peripheral;
        const_cast<BLEDeviceInfo&>(*device).peripheral = nullptr;
        return peripheral;
    }
    return nullptr;
}

bool BLEManagerMacOS::waitForCharacteristics(int timeout_ms) {
    std::unique_lock lock(characteristics_mutex_);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (!write_characteristic_ || !notify_characteristic_) {
        if (characteristics_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            break;
        }
    }

    if (!write_characteristic_) {
        std::cerr << "[BLEManagerMacOS] Timed out waiting for write characteristic ("
                  << timeout_ms << "ms)" << std::endl;
        return false;
    }
    if (!notify_characteristic_) {
        std::cerr << "[BLEManagerMacOS] Timed out waiting for notify characteristic ("
                  << timeout_ms << "ms)" << std::endl;
        return false;
    }

    return true;
}

} // namespace vehicle_sim