#pragma once

#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"

#include <atomic>
#include <condition_variable>

// Forward declarations for CoreBluetooth to avoid including Objective-C headers in C++
#ifdef __APPLE__
    #ifdef __OBJC__
        #import <CoreBluetooth/CoreBluetooth.h>
        #import <dispatch/dispatch.h>
    #else
        typedef struct objc_object CBCentralManager;
        typedef struct objc_object CBPeripheral;
        typedef struct objc_object CBCharacteristic;
        // Opaque dispatch-queue handle (matches dispatch_queue_t in <dispatch/dispatch.h>).
        typedef struct dispatch_queue_s* dispatch_queue_t;
    #endif
#endif

namespace vehicle_sim {

class BLEManageriOS : public BLEManagerBase {
public:
    BLEManageriOS();
    ~BLEManageriOS() override;

    std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds) override;
    bool connect(std::string_view device_identifier) override;
    void disconnect() override;
    void send(const std::vector<uint8_t>& data) override;
    bool isConnected() const override;
    std::string getConnectedDeviceId() const override;

    int getBluetoothState() const;
    bool isBluetoothReady() const;

    // Callback handlers (called by Objective-C delegate)
    void onDeviceDiscovered(const BLEDeviceInfo& device);
    void onConnectionStateChanged(bool is_connected, const std::string& device_id);
    void onServicesDiscovered();
    void onCharacteristicsDiscovered();
    void onCharacteristicNotification(const std::vector<uint8_t>& data);
    void onBluetoothStateChanged(bool isPoweredOn);
    void onDataReceived(const std::vector<uint8_t>& data);

    // Public wrapper for base class protected method (needed by ObjC delegate)
    void addDevice(const BLEDeviceInfo& device) { deviceRegistry().addDiscoveredDevice(device); }

    // Callback for characteristic discovery (called by ObjC delegate)
    void onCharacteristicDiscovered(CBCharacteristic* characteristic);

    bool waitForCharacteristics(int timeout_ms = 10000) override;

private:
#ifdef __APPLE__
    CBCentralManager* central_manager_ = nullptr;
    dispatch_queue_t ble_queue_ = nullptr;
    CBPeripheral* connected_peripheral_ = nullptr;
    CBCharacteristic* write_characteristic_ = nullptr;
    CBCharacteristic* notify_characteristic_ = nullptr;
    void* delegate_ = nullptr;
#endif

    std::atomic<bool> connected_;
    std::string connected_device_id_;

#ifdef __APPLE__
    std::mutex characteristics_mutex_;
    std::condition_variable characteristics_cv_;
#endif

    bool waitForBluetoothReady(int timeout_ms);
    CBPeripheral* findPeripheralByAddress(const std::string& address);
};

} // namespace vehicle_sim
