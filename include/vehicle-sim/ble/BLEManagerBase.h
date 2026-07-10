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
#include "vehicle-sim/ble/Elm327Session.h"
#include "vehicle-sim/boundary/ELM327Transport.h"

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
 *
 * The ELM327 OBD2 protocol, polling, CAN-monitor, VIN and auto-detection
 * responsibilities live in the composed Elm327Session (see Elm327Session.h);
 * this class is the slim BLE transport + device-discovery + connection-state
 * core, and exposes the public OBD2 API as thin forwards to that session.
 */
class BLEManagerBase : public Elm327SessionHost {
public:
    using DeviceCallback = std::function<void(const BLEDeviceInfo& device)>;
    using DataCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using ConnectionCallback = std::function<void(bool connected, const std::string& device_id)>;

    virtual ~BLEManagerBase() = default;

    // --- Elm327SessionHost: the transport facilities the composed session
    //     needs. These bridge the session to the BLE transport, connection
    //     state, and the consumer data callback.
    void sessionSendAscii(std::string_view command) override { sendASCII(command); }
    bool sessionIsConnected() const override { return connected_.load(); }
    void sessionDeliverParsed(const std::vector<uint8_t>& data) override {
        if (data_callback_) {
            data_callback_(data);
        }
    }

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
     * Access the composed ELM327 OBD2 session (roles 4+5: protocol, polling,
     * CAN monitor, VIN, auto-detection). The OBD2 API lives here, not on the
     * transport core, so callers reach it through this accessor — e.g.
     * `manager.elm327Session().initializeELM327()`. This keeps BLEManagerBase
     * a slim transport core (no god-class facade) while preserving the OBD2
     * surface verbatim on Elm327Session.
     */
    Elm327Session& elm327Session() { return session_; }
    const Elm327Session& elm327Session() const { return session_; }

    /**
     * Send an OBD2 PID query using ELM327 ASCII encoding.
     * @param pid The PID to query
     * @return Empty response - actual response comes via data callback
     */
    OBD2Response queryPID(uint8_t pid) {
        sendASCII(boundary::ELM327Transport::buildOBD2Query(OBD2_MODE_LIVE_DATA, pid));
        return OBD2Response{};
    }

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
    domain::VehicleDetector* vehicleDetector() { return session_.vehicleDetector(); }

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

    /**
     * @brief Construct with optional clock for time-dependent operations.
     * @param clock If null, uses SystemClock (real time); for tests, pass a FakeClock.
     */
    explicit BLEManagerBase(util::IClock* clock = nullptr);

    /**
     * @brief Set the clock for time-dependent operations.
     * Allows tests to inject a FakeClock after construction.
     */
    void setClock(util::IClock* clock) { session_.setClock(clock); }

    // --- State accessors (replace formerly-protected direct member access).
    //     The raw state members are now private (cpp:S3656); derived platform
    //     classes and test fixtures reach them only through these shims.
    void setConnected(bool c) noexcept { connected_ = c; }
    [[nodiscard]] bool isConnectedRaw() const noexcept { return connected_; }
    void setConnectedDeviceId(std::string id) { connected_device_id_ = std::move(id); }
    [[nodiscard]] const std::string& connectedDeviceIdRaw() const noexcept { return connected_device_id_; }
    std::vector<BLEDeviceInfo>& discoveredDevicesRaw() noexcept { return discovered_devices_; }
    [[nodiscard]] const std::vector<BLEDeviceInfo>& discoveredDevicesRaw() const noexcept { return discovered_devices_; }

    // Polling / protocol / RSSI constants (kept protected for derived + test access)
    static constexpr int DEFAULT_POLLING_INTERVAL_MS = Elm327Session::DEFAULT_POLLING_INTERVAL_MS;
    static constexpr int PROMPT_TIMEOUT_MS = Elm327Session::PROMPT_TIMEOUT_MS;
    static constexpr int POST_CONNECT_SETUP_DELAY_MS = Elm327Session::POST_CONNECT_SETUP_DELAY_MS;
    static constexpr uint8_t OBD2_MODE_LIVE_DATA = 0x01;    // Mode 01: Show Current Data
    static constexpr int RSSI_EXCELLENT = -50;
    static constexpr int RSSI_GOOD = -65;
    static constexpr int RSSI_FAIR = -75;

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
    void invokeDeviceCallback(const BLEDeviceInfo& device) const;

    /**
     * Invoke the data received callback (if set).
     * Thread-safe.
     */
    void invokeDataCallback(const std::vector<uint8_t>& data);

    /**
     * Invoke the connection callback (if set).
     * Thread-safe.
     */
    void invokeConnectionCallback(bool connected, std::string_view device_id) const;

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
     * Send an ASCII command string over BLE as raw bytes.
     */
    void sendASCII(std::string_view command);

    // --- Prompt / parse seams (thin forwards to the composed session).
    //     Kept protected so fixtures that drive the prompt sequence and parse
    //     helper keep compiling; the behaviour lives in Elm327Session.

    /// @copydoc Elm327Session::waitForPrompt(int)
    bool waitForPrompt(int timeout_ms = PROMPT_TIMEOUT_MS) { return session_.waitForPrompt(timeout_ms); }

    /// @copydoc Elm327Session::parseASCIIResponseToBinary(const std::vector<uint8_t>&) const
    std::vector<uint8_t> parseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData) const {
        return session_.parseASCIIResponseToBinary(asciiData);
    }

    /**
     * Signal that the ELM327 '>' prompt has been received.
     * Thin forward to the composed session; kept protected so existing
     * fixtures that drive the prompt sequence keep compiling.
     */
    void notifyPrompt() { session_.notifyPrompt(); }

    /**
     * Whether the adapter is in CAN monitor mode. Protected forward to the
     * composed session's flag (replaces the former protected can_mode_ member).
     */
    [[nodiscard]] bool canMode() const noexcept { return session_.canMode(); }

    /**
     * Whether a '>' prompt has been signalled and not yet consumed by a wait.
     * Protected forward to the composed session's prompt state (replaces the
     * former protected prompt_ready_ member).
     */
    bool promptReady() const { return session_.promptReady(); }

private:
    // Mutable synchronization primitives and the state they guard are private
    // so every access is channelled through this class's own (locking) methods,
    // enforcing proper synchronization. No derivative touches these directly.
    mutable std::mutex devices_mutex_;   // guards discovered_devices_
    mutable std::mutex raw_mutex_;       // guards last_raw_hex_
    std::string last_raw_hex_;

    // ================================================
    // Common State (was protected; moved private per cpp:S3656).
    // Derived classes and test fixtures reach these only via the protected
    // accessors (setConnected / isConnectedRaw / discoveredDevicesRaw / ...).
    // ================================================
    std::vector<BLEDeviceInfo> discovered_devices_;  // guarded by devices_mutex_

    std::atomic<bool> connected_{false};
    std::string connected_device_id_;

    // Callbacks (thread-safe via mutex where needed)
    DeviceCallback device_callback_;
    DataCallback data_callback_;
    ConnectionCallback connection_callback_;

    // The composed ELM327 OBD2 session (roles 4+5: protocol, polling, CAN
    // monitor, VIN, auto-detection). Holds the obd2_protocol handler, the
    // vehicle detector, prompt sequencing, the polling thread and can_mode.
    Elm327Session session_;

    // Raw BLE activity tracking (counts every notification before parsing).
    // Transport-level bookkeeping; stays on the transport core, not the session.
    std::atomic<int> ble_notification_count_{0};
};

} // namespace vehicle_sim