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
#include "vehicle-sim/ble/CallbackHub.h"
#include "vehicle-sim/ble/DeviceRegistry.h"
#include "vehicle-sim/ble/ConnectionState.h"
#include "vehicle-sim/ble/RawActivity.h"
#include "vehicle-sim/ble/OBD2Types.h"
#include "vehicle-sim/boundary/ELM327Transport.h"

namespace vehicle_sim {

/**
 * @brief Shared BLE functionality for both macOS and iOS.
 *
 * This class contains all common BLE logic following DRY principles:
 * - OBD2 command encoding/decoding (delegated to the composed Elm327Session)
 * - Response parsing and validation (delegated to the composed Elm327Session)
 * - Device storage, connection state, callbacks and raw-activity bookkeeping
 *   (each extracted into its own collaborator: DeviceRegistry, ConnectionState,
 *    CallbackHub, RawActivity)
 * - The ELM327 transport contract + connection-state primitives
 *
 * Platform-specific implementations (BLEManagerMacOS, BLEManageriOS) inherit
 * from this base class for CoreBluetooth operations. The god-class has been
 * decomposed (cpp:S1448): the movable responsibility clusters now live in the
 * private collaborators below and are reached through reference-returning
 * accessors (e.g. deviceRegistry(), connectionState(), callbacks(),
 * rawActivity()). The base no longer forwards to them — it delegates.
 *
 * The ELM327 OBD2 protocol, polling, CAN-monitor, VIN and auto-detection
 * responsibilities live in the composed Elm327Session (see Elm327Session.h).
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
    bool sessionIsConnected() const override { return connectionState().isConnectedRaw(); }
    void sessionDeliverParsed(const std::vector<uint8_t>& data) override {
        callbacks().invokeDataCallback(data);
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

    /**
     * Subscribe the consumer's device-found callback. This is the manager's
     * public subscription boundary; storage lives in CallbackHub (reached via
     * callbacks()).
     */
    virtual void setDeviceFoundCallback(DeviceCallback callback) {
        callbacks_.setDeviceFoundCallback(std::move(callback));
    }

    /**
     * Subscribe the consumer's raw-data callback. Public subscription boundary;
     * storage lives in CallbackHub (reached via callbacks()).
     */
    virtual void setDataReceivedCallback(DataCallback callback) {
        callbacks_.setDataReceivedCallback(std::move(callback));
    }

    // ================================================
    // Collaborator accessors (cpp:S1448 decomposition)
    //
    // Each returned reference is to a private member object that owns the
    // responsibility cluster previously living inline on this class. Callers
    // (platform subclasses, facade, tests) reach the behaviour through these
    // accessors — the base does NOT forward. This keeps BLEManagerBase a slim
    // transport core (no god-class) while preserving every observable behaviour.
    // ================================================

    /// Callback storage + invocation (device-found / data / connection).
    [[nodiscard]] CallbackHub& callbacks() noexcept { return callbacks_; }
    [[nodiscard]] const CallbackHub& callbacks() const noexcept { return callbacks_; }

    /// Discovered-device cache (dedup, lookup, clear).
    [[nodiscard]] DeviceRegistry& deviceRegistry() noexcept { return device_registry_; }
    [[nodiscard]] const DeviceRegistry& deviceRegistry() const noexcept { return device_registry_; }

    /// Connection state (connected flag + connected device id) + change notify.
    [[nodiscard]] ConnectionState& connectionState() noexcept { return connection_state_; }
    [[nodiscard]] const ConnectionState& connectionState() const noexcept { return connection_state_; }

    /// Raw BLE activity bookkeeping (notification count + last raw hex).
    [[nodiscard]] RawActivity& rawActivity() noexcept { return raw_activity_; }
    [[nodiscard]] const RawActivity& rawActivity() const noexcept { return raw_activity_; }

    // ================================================
    // OBD2 surface (delegated to the composed Elm327Session)
    // ================================================

    /**
     * Access the composed ELM327 OBD2 session (roles 4+5: protocol, polling,
     * CAN monitor, VIN, auto-detection). The OBD2 API lives here, not on the
     * transport core, so callers reach it through this accessor — e.g.
     * `manager.elm327Session().initializeELM327()`. This keeps BLEManagerBase
     * a slim transport core (no god-class facade) while preserving the OBD2
     * surface verbatim on Elm327Session.
     */
    [[nodiscard]] Elm327Session& elm327Session() { return session_; }
    [[nodiscard]] const Elm327Session& elm327Session() const { return session_; }

    /**
     * Get the vehicle detector for reading detection results. Kept as a session
     * forward because the facade (BLEManager::vehicleDetector) reaches it here.
     * @return Pointer to the active VehicleDetector (never null)
     */
    domain::VehicleDetector* vehicleDetector() { return session_.vehicleDetector(); }

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

    // Polling / protocol / RSSI constants (kept protected for derived + test access)
    static constexpr int DEFAULT_POLLING_INTERVAL_MS = Elm327Session::DEFAULT_POLLING_INTERVAL_MS;
    static constexpr int PROMPT_TIMEOUT_MS = Elm327Session::PROMPT_TIMEOUT_MS;
    static constexpr int POST_CONNECT_SETUP_DELAY_MS = Elm327Session::POST_CONNECT_SETUP_DELAY_MS;

    // ================================================
    // Protected Helper Methods for Derived Classes
    // ================================================

    /**
     * Send an ASCII command string over BLE as raw bytes.
     */
    void sendASCII(std::string_view command);

private:
    // Mutable synchronization primitives and the state they guard are private
    // so every access is channelled through this class's collaborators, which
    // enforce proper synchronization. No derivative touches the raw members
    // directly — they go through the public accessors (callbacks(),
    // deviceRegistry(), connectionState(), rawActivity()).

    // Callback storage (device/data/connection). Declared first: the other
    // collaborators hold a reference to it.
    CallbackHub callbacks_;

    // Discovered-device cache. Holds a reference to callbacks_ (fires
    // device-found on discovery).
    DeviceRegistry device_registry_{callbacks_};

    // Connection state. Holds a reference to callbacks_ (fires connection
    // callback on change).
    ConnectionState connection_state_{callbacks_};

    // The composed ELM327 OBD2 session (roles 4+5: protocol, polling, CAN
    // monitor, VIN, auto-detection). Holds the obd2_protocol handler, the
    // vehicle detector, prompt sequencing, the polling thread and can_mode.
    // Constructed before raw_activity_ (RawActivity routes raw bytes to it).
    Elm327Session session_{*this, nullptr};

    // Raw BLE activity tracking (counts every notification before parsing).
    // Holds a reference to session_ (routes raw bytes into the session for
    // parsing after bookkeeping).
    RawActivity raw_activity_{session_};
};

} // namespace vehicle_sim
