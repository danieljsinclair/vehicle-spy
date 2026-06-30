#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

#include "vehicle-sim/ble/BLEDeviceInfo.h"
#include "vehicle-sim/boundary/OBD2Protocol.h"
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/VehicleDetector.h"

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
    static constexpr uint8_t BATTERY_VOLTAGE = 0x42;        // V (control module voltage)
    static constexpr uint8_t ENGINE_RUNTIME = 0x1F;         // seconds since engine start
    static constexpr uint8_t BRAKE_PRESSURE = 0xA4;         // Manufacturer specific
    static constexpr uint8_t ACCELERATOR_POSITION_D = 0x5A; // Driver pedal %
    static constexpr uint8_t ACCELERATOR_POSITION_P = 0x5C; // Passenger pedal %
};

/**
 * @brief Parsed OBD2 response containing a telemetry value.
 * @deprecated This struct is kept for backward compatibility.
 *            OBD2 parsing should use ELM327Transport and OBD2Protocol instead.
 */
struct OBD2Response {
    uint8_t mode = 0;
    uint8_t pid = 0;
    std::vector<uint8_t> data;
    std::optional<double> value;
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
    virtual bool connect(std::string_view device_identifier) = 0;

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
     * Wait for write and notify characteristics to be discovered after connect.
     * Blocks until both are found or timeout expires.
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if both characteristics discovered
     */
    virtual bool waitForCharacteristics(int /*timeout_ms*/ = 10000) { return true; }

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
     * Send an OBD2 PID query using ELM327 ASCII encoding.
     * @param pid The PID to query
     * @return Empty response - actual response comes via data callback
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
     * Initialize ELM327 for CAN monitor mode.
     * Sends CAN init sequence (ATZ, ATE0, ATSP6, ATH1, ATMA).
     * @return true if initialization commands sent
     */
    bool initializeCANMonitor();

    /**
     * Initialize ELM327 for VIN query (safe, no protocol probing).
     * Uses ATSP6 (specific CAN protocol) instead of ATSP0 (auto-probe).
     * @return true if initialization commands sent
     */
    bool initializeForVINQuery();

    /**
     * Query vehicle VIN via OBD2 Mode 09 PID 02.
     * Must call initializeForVINQuery() first.
     * Blocks until VIN response received or timeout.
     * @param timeout_ms Maximum wait for response (default 5000ms)
     * @return VIN string (17 chars) if received, nullopt otherwise
     */
    std::optional<std::string> queryVIN(int timeout_ms = 5000);

    /**
     * Start CAN monitor mode.
     * In CAN mode, ELM327 continuously streams CAN frames after ATMA.
     * No polling loop needed — data arrives via BLE notifications.
     * @param interval_ms Unused for CAN mode (kept for API compatibility)
     */
    void startCANMonitor(int interval_ms = 200);

    /**
     * Stop CAN monitor mode.
     */
    void stopCANMonitor();

    /**
     * Initialize OBD2 protocol with auto-detection.
     * Sends AT commands, queries VIN and fuel type, and returns detection result.
     * @return Vehicle detection result if successful, nullopt otherwise
     */
    std::optional<domain::VehicleDetectionResult> initializeOBD2WithDetection();

    /**
     * Process incoming ASCII data from ELM327 adapter.
     * Routes data to OBD2Protocol handler which manages vehicle detection.
     * @param asciiData Raw ASCII response from adapter
     */
    void processOBD2Data(std::string_view asciiData);

    /**
     * Convert RSSI to signal quality string.
     * @param rssi Signal strength in dBm
     * @return Human-readable quality (Excellent/Good/Fair/Poor)
     */
    static std::string signalQuality(int rssi);

    /**
     * Get the vehicle detector for reading detection results.
     * @return Pointer to the active VehicleDetector (never null)
     */
    domain::VehicleDetector* vehicleDetector() { return vehicle_detector_.get(); }

    /**
     * Get count of raw BLE notifications received (before any parsing).
     * Increments on every invokeDataCallback call.
     */
    [[nodiscard]] int bleNotificationCount() const noexcept { return ble_notification_count_.load(); }

    /**
     * Get hex dump of the last raw bytes received from BLE (before parsing).
     */
    [[nodiscard]] std::string lastRawHex() const {
        std::scoped_lock lock(raw_mutex_);
        return last_raw_hex_;
    }

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

    std::atomic<bool> connected_{false};
    std::string connected_device_id_;

    // Callbacks (thread-safe via mutex where needed)
    DeviceCallback device_callback_;
    DataCallback data_callback_;
    ConnectionCallback connection_callback_;

    // Polling timing constants
    static constexpr int DEFAULT_POLLING_INTERVAL_MS = 200;
    static constexpr int PROMPT_TIMEOUT_MS = 2000;           // max wait for '>' prompt before skipping PID
    static constexpr int POST_CONNECT_SETUP_DELAY_MS = 500;  // wait for characteristic notifications

    // OBD2 protocol constants
    static constexpr uint8_t OBD2_MODE_LIVE_DATA = 0x01;    // Mode 01: Show Current Data

    // RSSI signal quality thresholds (dBm)
    static constexpr int RSSI_EXCELLENT = -50;
    static constexpr int RSSI_GOOD = -65;
    static constexpr int RSSI_FAIR = -75;

    // OBD2 polling state
    std::atomic<bool> polling_active_{false};
    std::thread polling_thread_;
    int polling_interval_ms_ = DEFAULT_POLLING_INTERVAL_MS;

    // ELM327 prompt-driven sequencing state
    // The ELM327 sends '>' when ready for the next command.
    // BLE notifications may fragment ELM327 responses, so we buffer
    // incoming data and scan for '>' across notification boundaries.
    std::mutex prompt_mutex_;
    std::condition_variable prompt_cv_;
    bool prompt_ready_ = false;
    std::string prompt_buffer_;
    static constexpr size_t PROMPT_BUFFER_MAX = 256;

    // OBD2 protocol handler for vehicle detection and command management
    boundary::OBD2Protocol obd2_protocol_;

    // CAN monitor mode flag
    std::atomic<bool> can_mode_{false};

    // Vehicle auto-detection (passive CAN ID observation)
    std::unique_ptr<domain::VehicleDetector> vehicle_detector_{std::make_unique<domain::VehicleDetector>()};

    // Raw BLE activity tracking (counts every notification before parsing)
    std::atomic<int> ble_notification_count_{0};
    mutable std::mutex raw_mutex_;
    std::string last_raw_hex_;

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
    void invokeConnectionCallback(bool connected, std::string_view device_id);

    /**
     * Update connection state and invoke callback.
     */
    void setConnectionState(bool connected, std::string_view device_id = "");

    /**
     * Get the discovered device by address.
     * Thread-safe.
     */
    std::optional<BLEDeviceInfo> findDeviceByAddress(std::string_view address) const;

    /**
     * Parse ASCII response from ELM327 adapter to binary OBD2 data.
     * @param asciiData Raw ASCII bytes from BLE notification
     * @return Binary OBD2 data, or empty if not a valid OBD2 response
     */
    std::vector<uint8_t> parseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData);

    /**
     * Send an ASCII command string over BLE as raw bytes.
     */
    void sendASCII(std::string_view command);

    /**
     * Send a sequence of AT commands, waiting for the ELM327 '>' prompt
     * after each one before sending the next.
     */
    void sendPromptDrivenSequence(const std::vector<boundary::ATCommand>& commands);

    /**
     * Wait for ELM327 '>' prompt with timeout.
     * Blocks until prompt is detected or timeout expires.
     * @param timeout_ms Maximum time to wait in milliseconds
     * @return true if prompt received, false if timed out
     */
    bool waitForPrompt(int timeout_ms = PROMPT_TIMEOUT_MS);

    /**
     * Signal that the ELM327 '>' prompt has been received.
     * Called from invokeDataCallback when '>' is detected in raw BLE data.
     */
    void notifyPrompt();

private:
    /**
     * Body of the OBD2 polling thread.
     * Waits for characteristic setup, then loops through the standard PIDs,
     * sending prompt-driven queries until polling is stopped or the
     * connection drops.
     */
    void obd2PollingLoop();
};

} // namespace vehicle_sim