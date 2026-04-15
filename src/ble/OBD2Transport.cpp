#include "vehicle-sim/ble/OBD2Transport.h"

#include <algorithm>

namespace vehicle_sim::ble {

OBD2Transport::OBD2Transport(std::unique_ptr<BLEPlatform> platform)
    : platform_(std::move(platform))
    , connected_(false)
{
    platform_->setDataReceivedCallback([this](const std::vector<std::uint8_t>& data) {
        this->processIncomingData(data);
    });
}

OBD2Transport::~OBD2Transport() {
    disconnect();
}

bool OBD2Transport::connect(const std::string& address) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (connected_) {
        return true;
    }

    connected_ = platform_->connect(address);
    return connected_;
}

void OBD2Transport::disconnect() {
    std::lock_guard<std::mutex> state_lock(state_mutex_);

    if (!connected_) {
        return;
    }

    platform_->disconnect();
    connected_ = false;

    {
        std::lock_guard<std::mutex> buffer_lock(buffer_mutex_);
        receive_buffer_.clear();
    }

    {
        std::lock_guard<std::mutex> callback_lock(callback_mutex_);
        notification_callback_ = nullptr;
    }
}

bool OBD2Transport::isConnected() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return connected_ && platform_->isConnected();
}

std::optional<std::vector<std::uint8_t>> OBD2Transport::readData() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (receive_buffer_.empty()) {
        return std::nullopt;
    }

    extractCleanResponses();

    if (receive_buffer_.empty()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> data(receive_buffer_.begin(), receive_buffer_.end());
    receive_buffer_.clear();
    return data;
}

void OBD2Transport::send(const std::vector<std::uint8_t>& data) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (!connected_) {
        return;
    }

    platform_->send(data);
}

void OBD2Transport::subscribeToNotifications(
    std::function<void(const std::vector<std::uint8_t>&)> callback
) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    notification_callback_ = std::move(callback);
}

void OBD2Transport::unsubscribe() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    notification_callback_ = nullptr;
}

void OBD2Transport::processIncomingData(const std::vector<std::uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        receive_buffer_.insert(receive_buffer_.end(), data.begin(), data.end());
    }

    extractCleanResponses();
}

void OBD2Transport::extractCleanResponses() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    // ELM327 terminates responses with '\r>' or '\r\r>'.
    // We look for the prompt character '>' (0x3E) to find response boundaries.
    // This avoids false splits on 0x0D which is also PID 0x0D (vehicle speed).

    while (!receive_buffer_.empty()) {
        // Find the prompt character '>' (0x3E) — marks end of ELM327 response
        auto promptPos = std::find(receive_buffer_.begin(), receive_buffer_.end(), 0x3E);

        if (promptPos == receive_buffer_.end()) {
            // No prompt yet — wait for more data
            return;
        }

        // Extract everything before the prompt
        std::vector<std::uint8_t> response(receive_buffer_.begin(), promptPos);

        // Remove the prompt and everything up to it
        receive_buffer_.erase(receive_buffer_.begin(), promptPos + 1);

        // Strip trailing CR (0x0D) characters from the response
        while (!response.empty() && response.back() == 0x0D) {
            response.pop_back();
        }

        if (!response.empty()) {
            forwardResponse(response);
        }
    }
}

void OBD2Transport::forwardResponse(const std::vector<std::uint8_t>& response) {
    std::lock_guard<std::mutex> lock(callback_mutex_);

    if (notification_callback_) {
        notification_callback_(response);
    }
}

} // namespace vehicle_sim::ble
