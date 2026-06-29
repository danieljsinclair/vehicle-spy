#include "vehicle-sim/ble/BLEManagerBase.h"
#include "vehicle-sim/boundary/OBD2Protocol.h"
#include "vehicle-sim/boundary/ELM327Transport.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <string_view>

namespace vehicle_sim {
namespace {

std::string padToWidth(std::string_view s, int width) {
    int displayWidth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        displayWidth += (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 ? 1 : 0;
    }
    if (displayWidth >= width) return std::string(s);
    return std::string(s) + std::string(width - displayWidth, ' ');
}

} // anonymous namespace

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
{
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

void BLEManagerBase::sendASCII(std::string_view command) {
    std::vector<uint8_t> bytes(command.begin(), command.end());
    send(bytes);
}

void BLEManagerBase::sendPromptDrivenSequence(const std::vector<boundary::ATCommand>& commands) {
    for (const auto& cmd : commands) {
        sendASCII(cmd.command);
        if (!waitForPrompt(PROMPT_TIMEOUT_MS)) {
            std::cerr << "[BLEManagerBase] Warning: no '>' prompt after " << cmd.command << std::endl;
        }
    }
}

OBD2Response BLEManagerBase::queryPID(uint8_t pid) {
    std::string cmd = boundary::ELM327Transport::buildOBD2Query(OBD2_MODE_LIVE_DATA, pid);
    sendASCII(cmd);
    return OBD2Response{};
}

std::vector<uint8_t> BLEManagerBase::parseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData) {
    // Convert bytes to string (ELM327 sends ASCII text)
    std::string response(asciiData.begin(), asciiData.end());

    // Use ELM327Transport to parse ASCII hex to binary
    auto binaryData = boundary::ELM327Transport::parseOBD2Response(response);

    if (binaryData) {
        return *binaryData;
    }

    // Not a valid OBD2 response - could be prompt, echo, or error
    return {};
}

bool BLEManagerBase::initializeELM327() {
    std::cout << "[BLEManagerBase] Initializing ELM327 adapter..." << std::endl;

    obd2_protocol_.setSendCallback([this](std::string_view cmd) { sendASCII(cmd); });

    // Send initialization sequence with prompt-driven pacing.
    // Each AT command must complete (ELM327 sends '>' prompt) before
    // sending the next. ATZ (reset) can take several seconds.
    sendPromptDrivenSequence(boundary::ELM327Transport::buildInitSequence());

    std::cout << "[BLEManagerBase] ELM327 initialization complete" << std::endl;
    return true;
}

std::optional<domain::VehicleDetectionResult> BLEManagerBase::initializeOBD2WithDetection() {
    std::cout << "[BLEManagerBase] Initializing ELM327 with auto-detection..." << std::endl;

    if (!initializeELM327()) {
        return std::nullopt;
    }

    std::cout << "[BLEManagerBase] Auto-detection ready - send queries and process responses" << std::endl;
    return obd2_protocol_.detectVehicle();
}

void BLEManagerBase::processOBD2Data(std::string_view asciiData) {
    obd2_protocol_.processIncomingData(asciiData);
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
        std::cout << "[BLEManagerBase] Starting OBD2 prompt-driven polling" << std::endl;

        // Wait a moment for characteristic notifications to be set up.
        // Chunked (10ms) so stopOBD2Polling() can interrupt this promptly — a
        // single sleep_for(POST_CONNECT_SETUP_DELAY_MS) would stall the join for
        // the full delay even after stop is requested.
        for (int waited = 0;
             waited < POST_CONNECT_SETUP_DELAY_MS && polling_active_;
             waited += 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const uint8_t pids[] = {
            OBD2PIDs::BATTERY_VOLTAGE,
            OBD2PIDs::ENGINE_LOAD,
            OBD2PIDs::COOLANT_TEMP,
            OBD2PIDs::THROTTLE_POSITION,
            OBD2PIDs::VEHICLE_SPEED,
            OBD2PIDs::ENGINE_RPM
        };

        while (polling_active_ && connected_) {
            for (uint8_t pid : pids) {
                if (!polling_active_ || !connected_) break;

                // Send query — ELM327 will respond with data followed by '>'
                queryPID(pid);

                // Wait for ELM327 '>' prompt before sending next query
                if (!waitForPrompt()) {
                    // Timed out waiting for prompt — adapter may be unresponsive.
                    // Skip to next PID rather than hanging.
                    std::cout << "[BLEManagerBase] Prompt timeout for PID 0x"
                              << std::hex << static_cast<int>(pid) << std::dec << std::endl;
                }
            }

            // Brief pause between full PID cycles to avoid hammering the bus
            std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval_ms_));
        }

        std::cout << "[BLEManagerBase] OBD2 polling stopped" << std::endl;
    });
}

void BLEManagerBase::stopOBD2Polling() {
    polling_active_ = false;
    // Wake the polling thread if it's waiting for a prompt
    notifyPrompt();
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
}

bool BLEManagerBase::waitForPrompt(int timeout_ms) {
    std::unique_lock lock(prompt_mutex_);
    prompt_ready_ = false;
    return prompt_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
        [this] { return prompt_ready_; });
}

void BLEManagerBase::notifyPrompt() {
    {
        std::scoped_lock lock(prompt_mutex_);
        prompt_ready_ = true;
    }
    prompt_cv_.notify_one();
}

bool BLEManagerBase::initializeCANMonitor() {
    std::cout << "[BLEManagerBase] Initializing ELM327 for CAN monitor mode..." << std::endl;

    auto commands = boundary::ELM327Transport::buildCANMonitorInitSequence();
    for (const auto& cmd : commands) {
        sendASCII(cmd.command);
        std::this_thread::sleep_for(std::chrono::milliseconds(cmd.delayMs));
    }

    can_mode_ = true;
    std::cout << "[BLEManagerBase] CAN monitor mode initialized" << std::endl;
    return true;
}

bool BLEManagerBase::initializeForVINQuery() {
    std::cout << "[BLEManagerBase] Initializing ELM327 for VIN query..." << std::endl;

    can_mode_ = false;
    vehicle_detector_->reset();

    sendPromptDrivenSequence(boundary::ELM327Transport::buildVINQueryInitSequence());

    std::cout << "[BLEManagerBase] VIN query initialization complete" << std::endl;
    return true;
}

std::optional<std::string> BLEManagerBase::queryVIN(int timeout_ms) {
    std::cout << "[BLEManagerBase] Querying VIN (09 02)..." << std::endl;

    can_mode_ = false;

    sendASCII(boundary::ELM327Transport::buildOBD2Query(0x09, 0x02));

    if (!waitForPrompt(timeout_ms)) {
        std::cout << "[BLEManagerBase] VIN query timed out" << std::endl;
        return std::nullopt;
    }

    auto result = vehicle_detector_->getResult();
    if (!result.vin.empty()) {
        std::cout << "[BLEManagerBase] VIN received: " << result.vin << std::endl;
        return result.vin;
    }

    std::cout << "[BLEManagerBase] No VIN in response" << std::endl;
    return std::nullopt;
}

void BLEManagerBase::startCANMonitor(int /*interval_ms*/) {
    // CAN monitor mode doesn't need a polling thread.
    // ELM327 streams CAN frames continuously after ATMA command.
    // Data arrives via BLE notifications → invokeDataCallback() → CAN frame parsing.
    can_mode_ = true;
    std::cout << "[BLEManagerBase] CAN monitor mode active - receiving frames via BLE notifications" << std::endl;
}

void BLEManagerBase::stopCANMonitor() {
    can_mode_ = false;
    sendASCII("ATMA\r");
    std::cout << "[BLEManagerBase] CAN monitor mode stopped" << std::endl;
}

// ================================================
// Device Management (Common Implementation)
// ================================================

void BLEManagerBase::addDiscoveredDevice(const BLEDeviceInfo& device) {
    std::scoped_lock lock(devices_mutex_);

    // Check for duplicates
    for (const auto& existing : discovered_devices_) {
        if (existing.address == device.address) {
            return;  // Already have this device
        }
    }

    discovered_devices_.push_back(device);
    std::cout << "  [" << std::setw(2) << discovered_devices_.size() << "] "
              << padToWidth(device.name, 36)
              << padToWidth(device.address, 40)
              << "RSSI:" << std::right << std::setw(4) << device.rssi
              << "  " << (device.isConnected ? "[Connected]" : "[Available]") << std::endl;

    // Invoke callback
    invokeDeviceCallback(device);
}

void BLEManagerBase::clearDiscoveredDevices() {
    std::scoped_lock lock(devices_mutex_);
    discovered_devices_.clear();
}

std::optional<BLEDeviceInfo> BLEManagerBase::findDeviceByAddress(std::string_view address) const {
    std::scoped_lock lock(devices_mutex_);

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
    // Always track raw BLE activity (before any parsing/dropping)
    ble_notification_count_++;
    {
        std::scoped_lock lock(raw_mutex_);
        std::ostringstream hex;
        for (size_t i = 0; i < std::min(data.size(), size_t(16)); ++i) {
            hex << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]) << " ";
        }
        if (data.size() > 16) hex << "...";
        last_raw_hex_ = hex.str();
    }

    // In OBD2 mode (not CAN monitor), detect ELM327 '>' prompt to drive
    // prompt-based query sequencing. BLE notifications may fragment ELM327
    // responses, so we buffer and scan for '>' across notification boundaries.
    if (!can_mode_) {
        prompt_buffer_.append(data.begin(), data.end());
        auto pos = prompt_buffer_.find('>');
        if (pos != std::string::npos) {
            prompt_buffer_.erase(0, pos + 1);
            notifyPrompt();
        }
        if (prompt_buffer_.size() > PROMPT_BUFFER_MAX) {
            prompt_buffer_.erase(0, prompt_buffer_.size() - PROMPT_BUFFER_MAX);
        }
    }

    if (!data_callback_) {
        return;
    }

    std::vector<uint8_t> binaryData;

    if (can_mode_) {
        std::string asciiStr(data.begin(), data.end());
        auto frame = boundary::ELM327Transport::parseCANFrame(asciiStr);
        if (frame && frame->data.size() == 8) {
            binaryData.resize(10);
            binaryData[0] = frame->canId & 0xFF;
            binaryData[1] = (frame->canId >> 8) & 0xFF;
            std::copy(frame->data.begin(), frame->data.end(), binaryData.begin() + 2);
        }
    } else {
        binaryData = parseASCIIResponseToBinary(data);
    }

    if (!binaryData.empty()) {
        vehicle_detector_->observeFrame(binaryData);
        data_callback_(binaryData);
    }
}

void BLEManagerBase::invokeConnectionCallback(bool connected, std::string_view device_id) {
    if (connection_callback_) {
        connection_callback_(connected, std::string(device_id));
    }
}

void BLEManagerBase::setConnectionState(bool connected, std::string_view device_id) {
    connected_ = connected;
    connected_device_id_ = device_id;
    invokeConnectionCallback(connected, device_id);
}

} // namespace vehicle_sim