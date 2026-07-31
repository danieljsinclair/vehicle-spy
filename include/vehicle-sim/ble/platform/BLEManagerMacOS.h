#pragma once

#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"
#include <condition_variable>

// Forward declarations for CoreBluetooth to avoid including Objective-C headers in C++
#ifdef __APPLE__
    #ifdef __OBJC__
        #import <CoreBluetooth/CoreBluetooth.h>
        @class BLEMacOSDelegate;
    #else
        // Forward declarations when not in Objective-C mode
        using CBCentralManager = struct objc_object;
        using CBPeripheral = struct objc_object;
        using CBCharacteristic = struct objc_object;
        using CBService = struct objc_object;
        using BLEMacOSDelegate = struct objc_object;
    #endif
#endif

namespace vehicle_sim {

/**
 * @brief macOS BLE platform implementation using CoreBluetooth.
 *
 * Uses Apple's CoreBluetooth framework to scan, connect, and communicate
 * with BLE peripherals (OBD2 adapters, Tesla BLE adapters).
 * Requires macOS 12+ (Monterey) for full CoreBluetooth support.
 *
 * This implementation inherits from BLEManagerBase which provides:
 * - OBD2 command building and response parsing
 * - Device storage and callback management
 * - Common state handling
 *
 * Platform-specific code (CoreBluetooth delegate handling) remains here.
 */
class BLEManagerMacOS : public BLEManagerBase {
public:
    BLEManagerMacOS();
    ~BLEManagerMacOS() override;

    // BLEPlatform interface (required by BLEManagerBase)
    std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds) override;
    bool connect(std::string_view device_identifier) override;
    void disconnect() override;
    void send(const std::vector<uint8_t>& data) override;
    bool isConnected() const override;
    std::string getConnectedDeviceId() const override;

    /**
     * Get the current Bluetooth state for UI display.
     * @return CBManagerState value as int
     */
    int getBluetoothState() const;

    /**
     * Check if Bluetooth is powered on and ready.
     * @return true if ready for scanning/connection
     */
    bool isBluetoothReady() const;

    /**
     * Get the CoreBluetooth central manager (for advanced use).
     * @return Pointer to CBCentralManager or nullptr
     */
#ifdef __APPLE__
    CBCentralManager* getCentralManager() const { return central_manager_; }
#endif

    // Platform-specific callback handlers (needed by Objective-C delegate)
    void onDeviceDiscovered(const BLEDeviceInfo& device);
    void onDataReceived(const std::vector<uint8_t>& data);
    void onConnectionStateChanged(bool is_connected, const std::string& device_id);
    void onServicesDiscovered();
    void onCharacteristicDiscovered(CBCharacteristic* characteristic);
    void onBluetoothStateChanged(bool isPoweredOn);

    // Public wrapper for base class protected method (needed by delegate)
    void addDevice(const BLEDeviceInfo& device) { deviceRegistry().addDiscoveredDevice(device); }

    /**
     * Wait for write and notify characteristics to be discovered.
     * Blocks until both are found or timeout expires.
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if both characteristics discovered
     */
    bool waitForCharacteristics(int timeout_ms = 10000) override;

private:
#ifdef __APPLE__
    // CoreBluetooth central manager (platform-specific)
    CBCentralManager* central_manager_ = nullptr;
    CBPeripheral* connected_peripheral_ = nullptr;
    CBCharacteristic* write_characteristic_ = nullptr;
    CBCharacteristic* notify_characteristic_ = nullptr;

    // Objective-C delegate for CoreBluetooth callbacks.
    // Opaque pointer typed as the delegate class (forward-declared above) to
    // keep this header C++ compatible while avoiding `void*`.
    BLEMacOSDelegate* delegate_ = nullptr;
#endif

    // Platform-specific helper methods
    bool waitForBluetoothReady(int timeout_ms);
    CBPeripheral* findPeripheralByAddress(const std::string& address);

#ifdef __APPLE__
    std::mutex characteristics_mutex_;
    std::condition_variable characteristics_cv_;
#endif
};

} // namespace vehicle_sim
