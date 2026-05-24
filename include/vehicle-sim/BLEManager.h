#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/ble/BLEDeviceInfo.h"
#include "vehicle-sim/domain/VehicleDetector.h"

namespace vehicle_sim {

/**
 * @brief High-level BLE manager that selects platform at runtime.
 *
 * This is the main entry point for BLE operations. It selects the appropriate
 * platform implementation (macOS or iOS) based on the build target.
 *
 * Usage:
 *   auto ble_manager = std::make_unique<BLEManager>();
 *   auto devices = ble_manager->scanForDevices(5);
 *   ble_manager->connect(device_address);
 *   ble_manager->onDataReceived([](const auto& data) { ... });
 */
class BLEManager {
public:
    using DeviceCallback = std::function<void(const BLEDeviceInfo& device)>;
    using DataCallback = std::function<void(const std::vector<uint8_t>& data)>;

    BLEManager();
    ~BLEManager();

    // Scan for BLE devices
    std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds = 5);

    // Connect to a specific device by address or UUID (platform-dependent)
    bool connect(const std::string& device_identifier);

    // Disconnect from current device
    void disconnect();

    // Subscribe to device discovery events
    void onDeviceFound(DeviceCallback callback);

    // Subscribe to raw BLE data
    void onDataReceived(DataCallback callback);

    // Check connection status
    bool isConnected() const;

    // Get currently connected device identifier
    std::string getConnectedDeviceId() const;

    // Set the BLE platform implementation (for testing/configuration)
    void setPlatform(std::unique_ptr<BLEManagerBase> platform);

    // Get the underlying platform (for advanced usage)
    BLEManagerBase* getPlatform() const;

    // Wait for write + notify characteristics after connect (blocks until ready or timeout)
    bool waitForCharacteristics(int timeout_ms = 10000);

    // Initialize ELM327 adapter (send AT commands after connection)
    bool initializeELM327();

    /**
     * @brief Initialize OBD2 protocol with auto-detection.
     * Sends AT commands, queries VIN and fuel type, and returns detection result.
     * @return Vehicle detection result if successful, nullopt otherwise
     */
    std::optional<domain::VehicleDetectionResult> initializeOBD2WithDetection();

    /**
     * @brief Process incoming ASCII data from ELM327 adapter.
     * Routes data to OBD2Protocol handler which manages vehicle detection.
     * @param asciiData Raw ASCII response from adapter
     */
    void processOBD2Data(const std::string& asciiData);

    // Start OBD2 PID polling at specified interval
    void startOBD2Polling(int interval_ms = 200);

    // Stop OBD2 PID polling
    void stopOBD2Polling();

    // Initialize ELM327 for CAN monitor mode
    bool initializeCANMonitor();

    // Initialize ELM327 for VIN query (safe, no protocol probing)
    bool initializeForVINQuery();

    // Query vehicle VIN via OBD2 Mode 09 PID 02 (blocks until response or timeout)
    std::optional<std::string> queryVIN(int timeout_ms = 5000);

    // Start CAN monitor mode (no polling thread needed)
    void startCANMonitor(int interval_ms = 200);

    // Stop CAN monitor mode
    void stopCANMonitor();

    // Get the vehicle detector for reading detection results
    domain::VehicleDetector* vehicleDetector();

    // Raw BLE activity tracking
    [[nodiscard]] int bleNotificationCount() const;
    [[nodiscard]] std::string lastRawHex() const;

private:
    std::unique_ptr<BLEManagerBase> platform_;
    DeviceCallback device_callback_;
    DataCallback data_callback_;

    // Factory method to create platform instance based on build target
    static std::unique_ptr<BLEManagerBase> createDefaultPlatform();
};

} // namespace vehicle_sim