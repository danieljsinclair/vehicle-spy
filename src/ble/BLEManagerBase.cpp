#include "vehicle-sim/ble/BLEManagerBase.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

namespace vehicle_sim {

// ================================================
// OBD2 UUIDs - Already defined in header as constexpr
// ================================================

// ================================================
// OBD2 PIDs - Already defined in header as constexpr
// ================================================

// ================================================
// BLEManagerBase Implementation
// ================================================

BLEManagerBase::BLEManagerBase()
    : connected_(false) {
}

void BLEManagerBase::setDeviceFoundCallback(DeviceCallback callback) {
    device_callback_ = std::move(callback);
}

void BLEManagerBase::setDataReceivedCallback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

void BLEManagerBase::setConnectionCallback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
}

// ================================================
// OBD2 Command Building
// ================================================

std::vector<uint8_t> BLEManagerBase::buildOBD2Query(uint8_t pid) const {
    // Standard OBD2 query format: [Mode] [PID] [Terminator]
    // Mode 01 = Show Live Data
    std::vector<uint8_t> cmd = {OBD2_MODE_LIVE_DATA, pid};
    // Many adapters expect carriage return terminator
    return cmd;
}

std::vector<uint8_t> BLEManagerBase::buildMode01Request(uint8_t pid) const {
    // Mode 01 request - standard OBD2 live data query
    // Format: "01 PID\r" as ASCII for most ELM327 adapters
    std::vector<uint8_t> cmd;
    cmd.push_back(OBD2_MODE_LIVE_DATA);  // Mode 01: Show Current Data
    cmd.push_back(pid);   // PID code

    // Some adapters also accept ASCII format - but we use binary for CoreBluetooth
    // The terminating CR will be handled by the write operation

    return cmd;
}

// ================================================
// OBD2 Response Parsing
// ================================================

OBD2Response BLEManagerBase::parseOBD2Response(const std::vector<uint8_t>& response) const {
    OBD2Response result;

    if (!validateOBD2Response(response)) {
        std::cerr << "[BLEManagerBase] Invalid OBD2 response format" << std::endl;
        return result;
    }

    result.mode = response[0];
    result.pid = response[1];

    // Data starts at byte 2 (mode + pid + maybe length byte)
    size_t data_start = DATA_OFFSET;
    if (response.size() > DATA_OFFSET && response[DATA_OFFSET] <= RESPONSE_MODE_MIN) {
        // Some responses have a length byte
        data_start = DATA_OFFSET + 1;
    }

    // Copy data bytes
    if (data_start < response.size()) {
        result.data.assign(response.begin() + data_start, response.end());
    }

    // Extract numeric value
    result.value = extractOBD2Value(result.data, result.pid);
    result.valid = result.value.has_value();

    if (result.valid) {
        std::cout << "[BLEManagerBase] Parsed PID 0x"
                  << std::hex << (int)result.pid << std::dec
                  << " = " << *result.value << std::endl;
    }

    return result;
}

std::optional<double> BLEManagerBase::extractOBD2Value(const std::vector<uint8_t>& data, uint8_t pid) const {
    if (data.empty()) {
        return std::nullopt;
    }

    try {
        return parseSpecificPID(pid, data);
    } catch (const std::exception& e) {
        std::cerr << "[BLEManagerBase] Error parsing PID 0x"
                  << std::hex << (int)pid << std::dec
                  << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

double BLEManagerBase::parseSpecificPID(uint8_t pid, const std::vector<uint8_t>& data) const {
    // OBD2 scaling formulas from SAE J1979
    switch (pid) {
        case OBD2PIDs::THROTTLE_POSITION:  // 0x11
            // A/255 * 100 = percentage
            return (data[0] / 255.0) * 100.0;

        case OBD2PIDs::VEHICLE_SPEED:  // 0x0D
            // Raw value in km/h
            return static_cast<double>(data[0]);

        case OBD2PIDs::ENGINE_RPM:  // 0x0C
            // ((A * 256) + B) / 4 = RPM
            if (data.size() >= 2) {
                return ((data[0] * 256.0) + data[1]) / OBD2_RPM_DIVISOR;
            }
            return static_cast<double>(data[0]);

        case OBD2PIDs::COOLANT_TEMP:  // 0x05
            // A - 40 = Celsius
            return static_cast<double>(data[0]) - OBD2_TEMP_OFFSET;

        case OBD2PIDs::INTAKE_AIR_TEMP:  // 0x0F
            // A - 40 = Celsius
            return static_cast<double>(data[0]) - OBD2_TEMP_OFFSET;

        case OBD2PIDs::ENGINE_LOAD:  // 0x04
            // A/255 * 100 = percentage
            return (data[0] / 255.0) * 100.0;

        case OBD2PIDs::FUEL_LEVEL:  // 0x2F
            // A/255 * 100 = percentage
            return (data[0] / 255.0) * 100.0;

        case OBD2PIDs::ACCELERATOR_POSITION_D:  // 0x5A
            // A/255 * 100 = percentage
            return (data[0] / 255.0) * 100.0;

        case OBD2PIDs::ACCELERATOR_POSITION_P:  // 0x5C
            // A/255 * 100 = percentage
            return (data[0] / 255.0) * 100.0;

        default:
            // Generic: return first byte as-is
            if (!data.empty()) {
                return static_cast<double>(data[0]);
            }
            return 0.0;
    }
}

bool BLEManagerBase::validateOBD2Response(const std::vector<uint8_t>& response) const {
    // Minimum response: mode + pid + at least one data byte
    if (response.size() < DATA_OFFSET + 1) {
        return false;
    }

    // Mode 0x41 = Response to Mode 0x01 (Show Current Data)
    // Mode 0x4X would be response to other modes
    if (response[0] < RESPONSE_MODE_MIN || response[0] > RESPONSE_MODE_MAX) {
        // Not a standard OBD2 response mode
        // Could be an error code or non-standard response
        std::cerr << "[BLEManagerBase] Unusual response mode: 0x"
                  << std::hex << (int)response[0] << std::dec << std::endl;
    }

    return true;
}

OBD2Response BLEManagerBase::queryPID(uint8_t pid) {
    auto cmd = buildOBD2Query(pid);
    send(cmd);

    // The actual response will come via the data callback
    // This is a synchronous convenience - subclasses may override
    return OBD2Response{};  // Return empty - response comes async
}

bool BLEManagerBase::initializeELM327() {
    std::cout << "[BLEManagerBase] Initializing ELM327 adapter..." << std::endl;

    // AT commands to initialize ELM327
    // These should be sent sequentially with delays

    // Reset adapter
    std::vector<uint8_t> reset_cmd = {'A', 'T', 'Z', '\r'};
    send(reset_cmd);

    // Disable echo
    std::vector<uint8_t> echo_cmd = {'A', 'T', 'E', '0', '\r'};
    send(echo_cmd);

    // Disable line feeds
    std::vector<uint8_t> lf_cmd = {'A', 'T', 'L', '0', '\r'};
    send(lf_cmd);

    // Set protocol to auto
    std::vector<uint8_t> proto_cmd = {'A', 'T', 'S', 'P', '0', '\r'};
    send(proto_cmd);

    // Set headers off (standard OBD2)
    std::vector<uint8_t> header_cmd = {'A', 'T', 'H', '0', '\r'};
    send(header_cmd);

    // Set timeout
    std::vector<uint8_t> timeout_cmd = {'A', 'T', 'S', 'T', 'F', 'F', '\r'};
    send(timeout_cmd);

    std::cout << "[BLEManagerBase] ELM327 initialization commands sent" << std::endl;
    return true;
}

std::string BLEManagerBase::signalQuality(int rssi) {
    if (rssi >= RSSI_EXCELLENT) return "Excellent";
    if (rssi >= RSSI_GOOD) return "Good";
    if (rssi >= RSSI_FAIR) return "Fair";
    return "Poor";
}

// ================================================
// OBD2 Polling
// ================================================

void BLEManagerBase::startOBD2Polling(int interval_ms) {
    if (polling_active_) {
        return;  // Already polling
    }

    polling_interval_ms_ = interval_ms;
    polling_active_ = true;

    polling_thread_ = std::thread([this]() {
        std::cout << "[BLEManagerBase] Starting OBD2 polling at "
                  << polling_interval_ms_ << "ms intervals" << std::endl;

        // Wait a moment for characteristic notifications to be set up
        std::this_thread::sleep_for(std::chrono::milliseconds(POST_CONNECT_SETUP_DELAY_MS));

        while (polling_active_ && connected_) {
            // Query each PID sequentially
            // Throttle position
            queryPID(OBD2PIDs::THROTTLE_POSITION);
            std::this_thread::sleep_for(std::chrono::milliseconds(PID_QUERY_DELAY_MS));

            // Vehicle speed
            queryPID(OBD2PIDs::VEHICLE_SPEED);
            std::this_thread::sleep_for(std::chrono::milliseconds(PID_QUERY_DELAY_MS));

            // Engine RPM
            queryPID(OBD2PIDs::ENGINE_RPM);
            std::this_thread::sleep_for(std::chrono::milliseconds(PID_QUERY_DELAY_MS));

            // Engine load
            queryPID(OBD2PIDs::ENGINE_LOAD);
            std::this_thread::sleep_for(std::chrono::milliseconds(PID_QUERY_DELAY_MS));

            // Coolant temp
            queryPID(OBD2PIDs::COOLANT_TEMP);

            // Wait remaining interval time
            std::this_thread::sleep_for(std::chrono::milliseconds(
                polling_interval_ms_ - TOTAL_PID_QUERY_TIME_MS > 0 ? polling_interval_ms_ - TOTAL_PID_QUERY_TIME_MS : PID_QUERY_DELAY_MS
            ));
        }

        std::cout << "[BLEManagerBase] OBD2 polling stopped" << std::endl;
    });
}

void BLEManagerBase::stopOBD2Polling() {
    polling_active_ = false;
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
}

// ================================================
// Device Management (Common Implementation)
// ================================================

void BLEManagerBase::addDiscoveredDevice(const BLEDeviceInfo& device) {
    std::lock_guard<std::mutex> lock(devices_mutex_);

    // Check for duplicates
    for (const auto& existing : discovered_devices_) {
        if (existing.address == device.address) {
            return;  // Already have this device
        }
    }

    discovered_devices_.push_back(device);
    std::cout << "[BLEManagerBase] Added device: " << device.name
              << " (RSSI: " << device.rssi << ")" << std::endl;

    // Invoke callback
    invokeDeviceCallback(device);
}

void BLEManagerBase::clearDiscoveredDevices() {
    std::lock_guard<std::mutex> lock(devices_mutex_);
    discovered_devices_.clear();
}

std::optional<BLEDeviceInfo> BLEManagerBase::findDeviceByAddress(const std::string& address) const {
    std::lock_guard<std::mutex> lock(devices_mutex_);

    for (const auto& device : discovered_devices_) {
        if (device.address == address) {
            return device;
        }
    }

    return std::nullopt;
}

// ================================================
// Callback Invocation
// ================================================

void BLEManagerBase::invokeDeviceCallback(const BLEDeviceInfo& device) {
    if (device_callback_) {
        device_callback_(device);
    }
}

void BLEManagerBase::invokeDataCallback(const std::vector<uint8_t>& data) {
    if (data_callback_) {
        data_callback_(data);
    }
}

void BLEManagerBase::invokeConnectionCallback(bool connected, const std::string& device_id) {
    if (connection_callback_) {
        connection_callback_(connected, device_id);
    }
}

void BLEManagerBase::setConnectionState(bool connected, const std::string& device_id) {
    connected_ = connected;
    connected_device_id_ = device_id;
    invokeConnectionCallback(connected, device_id);
}

} // namespace vehicle_sim