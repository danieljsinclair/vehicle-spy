#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

#include "vehicle-sim/ble/BLEDeviceInfo.h"

namespace vehicle_sim {

/**
 * @brief Common OBD2 BLE UUIDs used by ELM327-compatible adapters.
 *
 * These UUIDs are standardized across most BLE OBD2 adapters (Vgate, OBDLink, etc.)
 */
struct OBD2UUIDs {
    // Nordic UART Service (NUS) - common for ELM327 BLE
    static constexpr const char* NUS_SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    static constexpr const char* NUS_TX_CHARACTERISTIC = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // Notify
    static constexpr const char* NUS_RX_CHARACTERISTIC = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // Write

    // Legacy OBD2 UART (older ELM327 clones)
    static constexpr const char* OBD2_SERVICE = "FFE0";
    static constexpr const char* OBD2_CHARACTERISTIC = "FFE1";

    // Standard BLE services
    static constexpr const char* BATTERY_SERVICE = "180F";
    static constexpr const char* DEVICE_INFO_SERVICE = "180A";
};

/**
 * @brief OBD2 PID constants for standard vehicle telemetry.
 *
 * These PIDs follow the OBD-II standard (SAE J1979).
 */
struct OBD2PIDs {
    // Mode 01 (Live Data) PIDs
    static constexpr uint8_t THROTTLE_POSITION = 0x11;      // 0-100%
    static constexpr uint8_t VEHICLE_SPEED = 0x0D;          // km/h
    static constexpr uint8_t ENGINE_RPM = 0x0C;            // RPM
    static constexpr uint8_t COOLANT_TEMP = 0x05;           // Celsius
    static constexpr uint8_t INTAKE_AIR_TEMP = 0x0F;        // Celsius
    static constexpr uint8_t ENGINE_LOAD = 0x04;            // 0-100%
    static constexpr uint8_t FUEL_LEVEL = 0x2F;             // 0-100%
    static constexpr uint8_t BRAKE_PRESSURE = 0xA4;         // Manufacturer specific
    static constexpr uint8_t ACCELERATOR_POSITION_D = 0x5A; // Driver pedal %
    static constexpr uint8_t ACCELERATOR_POSITION_P = 0x5C; // Passenger pedal %
};

/**
 * @brief Parsed OBD2 response containing a telemetry value.
 */
struct OBD2Response {
    uint8_t mode;           // Response mode (usually 0x41 for Mode 01 response)
    uint8_t pid;            // PID that was queried
    std::vector<uint8_t> data;  // Raw data bytes
    std::optional<double> value; // Parsed numeric value (if parseable)
    bool valid = false;
};

/**
 * @brief Shared BLE functionality for both macOS and iOS.
 *
 * This class contains all common BLE logic following DRY principles:
 * - OBD2 command encoding/decoding
 * - Response parsing and validation
 * - Device storage and callback management
 * - Connection state handling
 *
 * Platform-specific implementations (BLEManagerMacOS, BLEManageriOS)
 * should inherit from or use this base class for CoreBluetooth operations.
 */
class BLEManagerBase {
public:
    using DeviceCallback = std::function<void(const BLEDeviceInfo& device)>;
    using DataCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using ConnectionCallback = std::function<void(bool connected, const std::string& device_id)>;

    virtual ~BLEManagerBase() = default;

    // ================================================
    // Public API - Common BLE Operations
    // ================================================

    /**
     * Scan for BLE devices for a specified duration.
     * @param timeout_seconds How long to scan
     * @return List of discovered BLE devices
     */
    virtual std::vector<BLEDeviceInfo> scanForDevices(int timeout_seconds) = 0;

    /**
     * Connect to a specific device by identifier (UUID on Apple).
     * @param device_identifier Device UUID or address
     * @return true if connection initiated successfully
     */
    virtual bool connect(const std::string& device_identifier) = 0;

    /**
     * Disconnect from the current device.
     */
    virtual void disconnect() = 0;

    /**
     * Set callback for device discovery events.
     * @param callback Function to call when device is found
     */
    virtual void setDeviceFoundCallback(DeviceCallback callback);

    /**
     * Set callback for incoming BLE data.
     * @param callback Function to call when data received
     */
    virtual void setDataReceivedCallback(DataCallback callback);

    /**
     * Set callback for connection state changes.
     * @param callback Function to call when connection state changes
     */
    void setConnectionCallback(ConnectionCallback callback);

    /**
     * Send raw data to connected device.
     * @param data Bytes to send
     */
    virtual void send(const std::vector<uint8_t>& data) = 0;

    /**
     * Check if currently connected.
     * @return true if connected
     */
    virtual bool isConnected() const = 0;

    /**
     * Get identifier of connected device.
     * @return Device UUID or empty string
     */
    virtual std::string getConnectedDeviceId() const = 0;

    // ================================================
    // OBD2 Helper Methods (Common Implementation)
    // ================================================

    /**
     * Build an OBD2 PID query command.
     * @param pid The PID to query (e.g., 0x11 for throttle)
     * @return Complete OBD2 command bytes
     */
    std::vector<uint8_t> buildOBD2Query(uint8_t pid) const;

    /**
     * Build an OBD2 Mode 01 request (live data).
     * @param pid The PID to request
     * @return Complete command including terminator
     */
    std::vector<uint8_t> buildMode01Request(uint8_t pid) const;

    /**
     * Parse an OBD2 response and extract the numeric value.
     * @param response Raw response bytes from device
     * @return Parsed response structure with value
     */
    OBD2Response parseOBD2Response(const std::vector<uint8_t>& response) const;

    /**
     * Extract numeric value from response data bytes.
     * Handles single-byte, double-byte, and bit-encoded values.
     * @param data Data bytes from response
     * @param pid The PID that was queried (affects parsing)
     * @return Optional double value if parseable
     */
    std::optional<double> extractOBD2Value(const std::vector<uint8_t>& data, uint8_t pid) const;

    /**
     * Send an OBD2 PID query and return parsed response.
     * Convenience method that combines buildOBD2Query + send + parseOBD2Response.
     * @param pid The PID to query
     * @return Parsed response with value
     */
    OBD2Response queryPID(uint8_t pid);

    /**
     * Send AT commands to initialize ELM327 adapter.
     * Should be called after connection before querying PIDs.
     * @return true if initialization successful
     */
    bool initializeELM327();

    /**
     * Start polling OBD2 PIDs at a specified interval.
     * Sends queries for throttle, speed, RPM, etc. and returns
     * parsed values via the data callback.
     * @param interval_ms Polling interval in milliseconds
     */
    void startOBD2Polling(int interval_ms = 200);

    /**
     * Stop polling OBD2 PIDs.
     */
    void stopOBD2Polling();

    /**
     * Convert RSSI to signal quality string.
     * @param rssi Signal strength in dBm
     * @return Human-readable quality (Excellent/Good/Fair/Poor)
     */
    static std::string signalQuality(int rssi);

protected:
    // ================================================
    // Protected Constructor - For derived classes
    // ================================================

    BLEManagerBase();

    // ================================================
    // Common State (Shared by Derived Classes)
    // ================================================

    mutable std::mutex devices_mutex_;
    std::vector<BLEDeviceInfo> discovered_devices_;

    std::atomic<bool> connected_;
    std::string connected_device_id_;

    // Callbacks (thread-safe via mutex where needed)
    DeviceCallback device_callback_;
    DataCallback data_callback_;
    ConnectionCallback connection_callback_;

    // Polling timing constants
    static constexpr int DEFAULT_POLLING_INTERVAL_MS = 200;
    static constexpr int PID_QUERY_DELAY_MS = 50;           // delay between sequential PID queries
    static constexpr int TOTAL_PID_QUERY_TIME_MS = 250;      // 5 PIDs * 50ms
    static constexpr int POST_CONNECT_SETUP_DELAY_MS = 500;  // wait for characteristic notifications

    // OBD2 protocol constants
    static constexpr uint8_t OBD2_MODE_LIVE_DATA = 0x01;    // Mode 01: Show Current Data
    static constexpr double OBD2_MAX_BYTE = 255.0;           // 8-bit A value maximum
    static constexpr double OBD2_PERCENT_SCALE = 100.0;      // (A / 255) * 100
    static constexpr double OBD2_RPM_DIVISOR = 4.0;          // ((A * 256) + B) / 4
    static constexpr double OBD2_TEMP_OFFSET = 40.0;         // A - 40 (coolant/intake temp)
    static constexpr double OBD2_MULTI_BYTE_SCALE = 256.0;   // A * 256 + B scaling
    static constexpr std::size_t DATA_OFFSET = 2;             // skip mode + pid bytes
    static constexpr uint8_t RESPONSE_MODE_MIN = 0x40;       // 0x40-0x4F = Mode 01-0F responses
    static constexpr uint8_t RESPONSE_MODE_MAX = 0x4F;

    // RSSI signal quality thresholds (dBm)
    static constexpr int RSSI_EXCELLENT = -50;
    static constexpr int RSSI_GOOD = -65;
    static constexpr int RSSI_FAIR = -75;

    // OBD2 polling state
    std::atomic<bool> polling_active_{false};
    std::thread polling_thread_;
    int polling_interval_ms_ = DEFAULT_POLLING_INTERVAL_MS;

    // ================================================
    // Protected Helper Methods for Derived Classes
    // ================================================

    /**
     * Add a discovered device to the internal list.
     * Prevents duplicates based on address.
     */
    void addDiscoveredDevice(const BLEDeviceInfo& device);

    /**
     * Clear the discovered devices list.
     */
    void clearDiscoveredDevices();

    /**
     * Invoke the device found callback (if set).
     * Thread-safe.
     */
    void invokeDeviceCallback(const BLEDeviceInfo& device);

    /**
     * Invoke the data received callback (if set).
     * Thread-safe.
     */
    void invokeDataCallback(const std::vector<uint8_t>& data);

    /**
     * Invoke the connection callback (if set).
     * Thread-safe.
     */
    void invokeConnectionCallback(bool connected, const std::string& device_id);

    /**
     * Update connection state and invoke callback.
     */
    void setConnectionState(bool connected, const std::string& device_id = "");

    /**
     * Get the discovered device by address.
     * Thread-safe.
     */
    std::optional<BLEDeviceInfo> findDeviceByAddress(const std::string& address) const;

private:
    /**
     * Validate OBD2 response format.
     * Checks for proper header and length.
     */
    bool validateOBD2Response(const std::vector<uint8_t>& response) const;

    /**
     * Parse specific PID types to extract values.
     */
    double parseSpecificPID(uint8_t pid, const std::vector<uint8_t>& data) const;
};

} // namespace vehicle_sim