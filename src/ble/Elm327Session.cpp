#include "vehicle-sim/ble/Elm327Session.h"
#include "vehicle-sim/ble/BLEManagerBase.h"  // for OBD2PIDs / OBD2_MODE_LIVE_DATA constants
#include "vehicle-sim/boundary/ELM327Transport.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace vehicle_sim {
namespace {

// OBD2 Mode 01: Show Current Data (live-data query mode).
constexpr uint8_t OBD2_MODE_LIVE_DATA = 0x01;

} // anonymous namespace

// ================================================
// OBD2 PID constants (Mode 01 live-data) — reused from BLEManagerBase
// (OBD2PIDs is defined at vehicle_sim scope in BLEManagerBase.h).
// ================================================

Elm327Session::Elm327Session(Elm327SessionHost& host) : host_(host) {
    obd2_protocol_.setSendCallback(
        [this](std::string_view cmd) { host_.sessionSendAscii(cmd); });
}

Elm327Session::~Elm327Session() {
    stopOBD2Polling();
}

// ================================================
// ELM327 / OBD2 init & detection
// ================================================

bool Elm327Session::initializeELM327() {
    std::cout << "[Elm327Session] Initializing ELM327 adapter..." << std::endl;

    // Send initialization sequence with prompt-driven pacing. Each AT command
    // must complete (ELM327 sends '>' prompt) before the next; ATZ (reset)
    // can take several seconds.
    for (const auto& cmd : boundary::ELM327Transport::buildInitSequence()) {
        host_.sessionSendAscii(cmd.command);
        if (!waitForPrompt(PROMPT_TIMEOUT_MS)) {
            std::cerr << "[Elm327Session] Warning: no '>' prompt after " << cmd.command << std::endl;
        }
    }

    std::cout << "[Elm327Session] ELM327 initialization complete" << std::endl;
    return true;
}

std::optional<domain::VehicleDetectionResult> Elm327Session::initializeOBD2WithDetection() {
    std::cout << "[Elm327Session] Initializing ELM327 with auto-detection..." << std::endl;

    if (!initializeELM327()) {
        return std::nullopt;
    }

    std::cout << "[Elm327Session] Auto-detection ready - send queries and process responses" << std::endl;
    return obd2_protocol_.detectVehicle();
}

void Elm327Session::processOBD2Data(std::string_view asciiData) {
    obd2_protocol_.processIncomingData(asciiData);
}

// ================================================
// OBD2 polling
// ================================================

void Elm327Session::startOBD2Polling(int interval_ms) {
    if (polling_active_) {
        return;  // Already polling
    }

    polling_interval_ms_ = interval_ms;
    polling_active_ = true;

    polling_thread_ = std::thread([this]() { obd2PollingLoop(); });
}

void Elm327Session::obd2PollingLoop() {
    std::cout << "[Elm327Session] Starting OBD2 prompt-driven polling" << std::endl;

    // Wait a moment for characteristic notifications to be set up. Chunked
    // (10ms) so stopOBD2Polling() can interrupt promptly — a single
    // sleep_for(POST_CONNECT_SETUP_DELAY_MS) would stall the join for the
    // full delay even after stop is requested.
    for (int waited = 0;
         waited < POST_CONNECT_SETUP_DELAY_MS && polling_active_;
         waited += 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // PID order is preserved by construction — do NOT reorder (the contract
    // suite locks the first PID of each cycle: BATTERY_VOLTAGE first).
    const uint8_t pids[] = {
        OBD2PIDs::BATTERY_VOLTAGE,
        OBD2PIDs::ENGINE_LOAD,
        OBD2PIDs::COOLANT_TEMP,
        OBD2PIDs::THROTTLE_POSITION,
        OBD2PIDs::VEHICLE_SPEED,
        OBD2PIDs::ENGINE_RPM
    };

    while (polling_active_ && host_.sessionIsConnected()) {
        for (uint8_t pid : pids) {
            if (!polling_active_ || !host_.sessionIsConnected()) break;

            // Send query — ELM327 will respond with data followed by '>'.
            host_.sessionSendAscii(
                boundary::ELM327Transport::buildOBD2Query(OBD2_MODE_LIVE_DATA, pid));

            // Wait for ELM327 '>' prompt before sending the next query.
            if (!waitForPrompt()) {
                // Timed out waiting for prompt — adapter may be unresponsive.
                // Skip to next PID rather than hanging.
                std::cout << "[Elm327Session] Prompt timeout for PID 0x"
                          << std::hex << static_cast<int>(pid) << std::dec << std::endl;
            }
        }

        // Brief pause between full PID cycles to avoid hammering the bus.
        std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval_ms_));
    }

    std::cout << "[Elm327Session] OBD2 polling stopped" << std::endl;
}

void Elm327Session::stopOBD2Polling() {
    polling_active_ = false;
    // Wake the polling thread if it's waiting for a prompt.
    notifyPrompt();
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
}

// ================================================
// Prompt sequencing
// ================================================

bool Elm327Session::waitForPrompt(int timeout_ms) {
    std::unique_lock lock(prompt_mutex_);
    prompt_ready_ = false;
    return prompt_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
        [this] { return prompt_ready_; });
}

void Elm327Session::notifyPrompt() {
    {
        std::scoped_lock lock(prompt_mutex_);
        prompt_ready_ = true;
    }
    prompt_cv_.notify_one();
}

// ================================================
// CAN monitor mode
// ================================================

bool Elm327Session::initializeCANMonitor() {
    std::cout << "[Elm327Session] Initializing ELM327 for CAN monitor mode..." << std::endl;

    auto commands = boundary::ELM327Transport::buildCANMonitorInitSequence();
    for (const auto& cmd : commands) {
        host_.sessionSendAscii(cmd.command);
        std::this_thread::sleep_for(std::chrono::milliseconds(cmd.delayMs));
    }

    can_mode_ = true;
    std::cout << "[Elm327Session] CAN monitor mode initialized" << std::endl;
    return true;
}

void Elm327Session::startCANMonitor(int /*interval_ms*/) {
    // CAN monitor mode doesn't need a polling thread. ELM327 streams CAN
    // frames continuously after ATMA; data arrives via BLE notifications →
    // host → handleIncomingData() → CAN frame parsing.
    can_mode_ = true;
    std::cout << "[Elm327Session] CAN monitor mode active - receiving frames via BLE notifications" << std::endl;
}

void Elm327Session::stopCANMonitor() {
    can_mode_ = false;
    host_.sessionSendAscii("ATMA\r");
    std::cout << "[Elm327Session] CAN monitor mode stopped" << std::endl;
}

// ================================================
// VIN query
// ================================================

bool Elm327Session::initializeForVINQuery() {
    std::cout << "[Elm327Session] Initializing ELM327 for VIN query..." << std::endl;

    can_mode_ = false;
    vehicle_detector_->reset();

    for (const auto& cmd : boundary::ELM327Transport::buildVINQueryInitSequence()) {
        host_.sessionSendAscii(cmd.command);
        if (!waitForPrompt(PROMPT_TIMEOUT_MS)) {
            std::cerr << "[Elm327Session] Warning: no '>' prompt after " << cmd.command << std::endl;
        }
    }

    std::cout << "[Elm327Session] VIN query initialization complete" << std::endl;
    return true;
}

std::optional<std::string> Elm327Session::queryVIN(int timeout_ms) {
    std::cout << "[Elm327Session] Querying VIN (09 02)..." << std::endl;

    can_mode_ = false;

    host_.sessionSendAscii(boundary::ELM327Transport::buildOBD2Query(0x09, 0x02));

    if (!waitForPrompt(timeout_ms)) {
        std::cout << "[Elm327Session] VIN query timed out" << std::endl;
        return std::nullopt;
    }

    if (auto result = vehicle_detector_->getResult(); !result.vin.empty()) {
        std::cout << "[Elm327Session] VIN received: " << result.vin << std::endl;
        return result.vin;
    }

    std::cout << "[Elm327Session] No VIN in response" << std::endl;
    return std::nullopt;
}

// ================================================
// Data path
// ================================================

std::vector<uint8_t> Elm327Session::parseASCIIResponseToBinary(const std::vector<uint8_t>& asciiData) const {
    // Convert bytes to string (ELM327 sends ASCII text).
    std::string response(asciiData.begin(), asciiData.end());

    // Use ELM327Transport to parse ASCII hex to binary. Not a valid OBD2
    // response (prompt, echo, or error) yields an empty payload. value_or({})
    // is non-deduced, so the empty vector type is spelled explicitly.
    auto binaryData = boundary::ELM327Transport::parseOBD2Response(response);
    return binaryData.value_or(std::vector<uint8_t>{});
}

void Elm327Session::handleIncomingData(const std::vector<uint8_t>& data) {
    // In OBD2 mode (not CAN monitor), detect the ELM327 '>' prompt to drive
    // prompt-based query sequencing. BLE notifications may fragment ELM327
    // responses, so buffer and scan for '>' across notification boundaries.
    if (!can_mode_) {
        prompt_buffer_.append(data.begin(), data.end());
        if (auto pos = prompt_buffer_.find('>'); pos != std::string::npos) {
            prompt_buffer_.erase(0, pos + 1);
            notifyPrompt();
        }
        if (prompt_buffer_.size() > PROMPT_BUFFER_MAX) {
            prompt_buffer_.erase(0, prompt_buffer_.size() - PROMPT_BUFFER_MAX);
        }
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
        host_.sessionDeliverParsed(binaryData);
    }
}

} // namespace vehicle_sim
