#include "vehicle-sim/ble/TeslaBLETransport.h"
#include <algorithm>

namespace vehicle_sim::ble {

TeslaBLETransport::TeslaBLETransport(std::unique_ptr<BLEPlatform> platform)
    : platform_(std::move(platform))
    , connected_(false)
    , notification_callback_(nullptr) {
    // Register callback with platform for incoming data
    platform_->setDataReceivedCallback([this](const std::vector<std::uint8_t>& data) {
        this->processIncomingData(data);
    });
}

TeslaBLETransport::~TeslaBLETransport() {
    TeslaBLETransport::disconnect();
}

bool TeslaBLETransport::connect(const std::string& address) {
    std::scoped_lock lock(state_mutex_);

    if (connected_) {
        return true; // Already connected
    }

    connected_ = platform_->connect(address);
    return connected_;
}

void TeslaBLETransport::disconnect() {
    std::scoped_lock state_lock(state_mutex_);

    if (!connected_) {
        return; // Already disconnected
    }

    platform_->disconnect();
    connected_ = false;

    // Clear receive buffer on disconnect
    {
        std::scoped_lock buffer_lock(buffer_mutex_);
        receive_buffer_.clear();
    }

    // Clear callback
    {
        std::scoped_lock callback_lock(callback_mutex_);
        notification_callback_ = nullptr;
    }
}

bool TeslaBLETransport::isConnected() const {
    std::scoped_lock lock(state_mutex_);
    return connected_ && platform_->isConnected();
}

std::optional<std::vector<std::uint8_t>> TeslaBLETransport::readData() {
    std::scoped_lock lock(buffer_mutex_);

    if (receive_buffer_.empty()) {
        return std::nullopt;
    }

    // Try to extract a complete packet from buffer
    extractCompletePackets();

    if (receive_buffer_.empty()) {
        return std::nullopt;
    }

    // Return what's available (may be partial packet)
    std::vector<std::uint8_t> data(receive_buffer_.begin(), receive_buffer_.end());
    receive_buffer_.clear();
    return data;
}

void TeslaBLETransport::send(const std::vector<std::uint8_t>& data) {
    std::scoped_lock lock(state_mutex_);

    if (!connected_) {
        return; // Silently ignore send when not connected
    }

    platform_->send(data);
}

void TeslaBLETransport::subscribeToNotifications(
    std::function<void(const std::vector<std::uint8_t>&)> callback) {
    std::scoped_lock lock(callback_mutex_);
    notification_callback_ = std::move(callback);
}

void TeslaBLETransport::unsubscribe() {
    std::scoped_lock lock(callback_mutex_);
    notification_callback_ = nullptr;
}

void TeslaBLETransport::processIncomingData(const std::vector<std::uint8_t>& data) {
    {
        std::scoped_lock lock(buffer_mutex_);
        receive_buffer_.insert(receive_buffer_.end(), data.begin(), data.end());
    }

    extractCompletePackets();
}

bool TeslaBLETransport::validatePacket(const std::vector<std::uint8_t>& packet) const {
    // Minimum packet size is 4 bytes: a 2-byte header, a 1-byte length field,
    // and a 1-byte checksum.
    if (packet.size() < 4) {
        return false;
    }

    // Validate header
    if (packet[0] != HEADER_BYTE_1 || packet[1] != HEADER_BYTE_2) {
        return false;
    }

    // Validate length field matches packet size
    if (std::uint8_t length = packet[2]; packet.size() != static_cast<size_t>(length + 4)) {
        return false;
    }

    // Validate checksum (last byte)
    std::uint8_t calculated_checksum = calculateChecksum(packet);
    std::uint8_t expected_checksum = packet.back();

    return calculated_checksum == expected_checksum;
}

std::uint8_t TeslaBLETransport::calculateChecksum(const std::vector<std::uint8_t>& data) const {
    // Checksum is sum of all bytes except the checksum byte itself
    std::uint32_t sum = 0;
    for (size_t i = 0; i < data.size() - 1; ++i) {
        sum += data[i];
    }
    return static_cast<std::uint8_t>(sum & 0xFF);
}

void TeslaBLETransport::extractCompletePackets() {
    std::scoped_lock lock(buffer_mutex_);

    while (receive_buffer_.size() >= 4) {
        // Look for valid header
        size_t header_pos = 0;
        bool header_found = false;

        for (size_t i = 0; i <= receive_buffer_.size() - 2; ++i) {
            if (receive_buffer_[i] == HEADER_BYTE_1 && receive_buffer_[i + 1] == HEADER_BYTE_2) {
                header_pos = i;
                header_found = true;
                break;
            }
        }

        if (!header_found) {
            // No valid header found, clear buffer
            receive_buffer_.clear();
            return;
        }

        // Remove any bytes before the valid header
        if (header_pos > 0) {
            receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.begin() + header_pos);
        }

        // Check if we have enough bytes for length field
        if (receive_buffer_.size() < 3) {
            return; // Wait for more data
        }

        // Get expected packet length
        std::uint8_t length = receive_buffer_[2];
        size_t expected_packet_size = length + 4; // header(2) + length(1) + payload(length) + checksum(1)

        // Check if we have complete packet
        if (receive_buffer_.size() < expected_packet_size) {
            return; // Wait for more data
        }

        // Extract packet
        std::vector<std::uint8_t> packet(
            receive_buffer_.begin(),
            receive_buffer_.begin() + expected_packet_size
        );

        // Remove packet from buffer
        receive_buffer_.erase(
            receive_buffer_.begin(),
            receive_buffer_.begin() + expected_packet_size
        );

        // Validate and forward packet
        if (validatePacket(packet)) {
            forwardPacket(packet);
        }
        // Invalid packets are silently discarded
    }
}

void TeslaBLETransport::forwardPacket(const std::vector<std::uint8_t>& packet) {
    std::scoped_lock lock(callback_mutex_);

    if (notification_callback_) {
        notification_callback_(packet);
    }
}

} // namespace vehicle_sim::ble
