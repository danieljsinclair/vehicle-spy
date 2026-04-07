#include "vehicle-sim/domain/TeslaSignalParser.h"

#include <mutex>
#include <chrono>
#include <algorithm>

namespace vehicle_sim::domain {

TeslaSignalParser::TeslaSignalParser(SignalCallback callback)
    : m_callback(std::move(callback))
{
}

std::optional<VehicleSignal> TeslaSignalParser::parseFrame(
    const std::vector<std::uint8_t>& frame
) const {
    if (!isValidFrame(frame)) {
        return std::nullopt;
    }

    const auto dataStart = 3; // Skip ID (2 bytes) + DLC (1 byte)
    const auto dataLength = frame[2];

    if (frame.size() < static_cast<std::size_t>(dataStart + dataLength + 1)) {
        return std::nullopt; // Incomplete frame
    }

    std::vector<std::uint8_t> data(frame.begin() + dataStart,
                                    frame.begin() + dataStart + dataLength);

    // Extract signals from data
    const double speed = extractSpeed(data);
    const double throttle = extractThrottle(data);
    const double brake = extractBrake(data);
    const double acceleration = extractAcceleration(data);

    VehicleSignal signal(throttle, speed, acceleration, brake, getCurrentTimestamp());

    return signal;
}

std::vector<std::vector<std::uint8_t>> TeslaSignalParser::feedData(
    const std::vector<std::uint8_t>& data
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_buffer.insert(m_buffer.end(), data.begin(), data.end());
    std::vector<std::vector<std::uint8_t>> completeFrames;

    std::size_t pos = 0;
    while (pos + 3 <= m_buffer.size()) {
        // Check if we have enough data to read DLC
        const std::uint8_t dlc = m_buffer[pos + 2];
        const std::size_t frameSize = 3 + dlc + 1; // ID + DLC + Data + Checksum

        if (pos + frameSize > m_buffer.size()) {
            // Not enough data for complete frame, keep in buffer
            break;
        }

        // Extract complete frame
        std::vector<std::uint8_t> frame(
            m_buffer.begin() + pos,
            m_buffer.begin() + pos + frameSize
        );

        if (isValidFrame(frame)) {
            completeFrames.push_back(frame);

            // Parse signal and invoke callback if set
            auto signal = parseFrame(frame);
            if (signal && m_callback) {
                m_callback(*signal);
            }
        }

        pos += frameSize;
    }

    // Remove processed data from buffer
    if (pos > 0 && pos <= m_buffer.size()) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + pos);
    }

    return completeFrames;
}

bool TeslaSignalParser::isValidFrame(
    const std::vector<std::uint8_t>& frame
) const {
    // Minimum frame: ID (2) + DLC (1) + Checksum (1) = 4 bytes
    if (frame.size() < 4) {
        return false;
    }

    const std::uint8_t dlc = frame[2];
    if (dlc > 8) {
        return false; // Invalid DLC
    }

    const std::size_t expectedSize = 3 + dlc + 1;
    if (frame.size() != expectedSize) {
        return false;
    }

    // Validate checksum
    const std::uint8_t expectedChecksum = calculateChecksum(
        std::vector<std::uint8_t>(frame.begin(), frame.end() - 1)
    );

    return frame.back() == expectedChecksum;
}

std::uint32_t TeslaSignalParser::extractCANId(
    const std::vector<std::uint8_t>& frame
) const {
    if (frame.size() < 2) {
        return 0;
    }

    // Standard CAN ID (11 bits)
    return static_cast<std::uint32_t>(frame[0]) |
           (static_cast<std::uint32_t>(frame[1]) << 8);
}

bool TeslaSignalParser::isExtendedCANId(
    const std::vector<std::uint8_t>& frame
) const {
    // Extended ID indicator could be in specific bits
    // For now, assume all frames are standard
    return false;
}

std::uint8_t TeslaSignalParser::calculateChecksum(
    const std::vector<std::uint8_t>& frame
) const {
    std::uint8_t checksum = 0;
    for (std::uint8_t byte : frame) {
        checksum += byte;
    }
    return checksum;
}

void TeslaSignalParser::setSignalCallback(SignalCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(callback);
}

void TeslaSignalParser::clearBuffer() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
}

double TeslaSignalParser::extractSpeed(
    const std::vector<std::uint8_t>& data
) const {
    // Simple extraction for test - first 4 bytes as speed
    if (data.size() >= 4) {
        const std::uint32_t rawSpeed =
            static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
        // Convert to km/h (simple scaling for test)
        return static_cast<double>(rawSpeed) / 1000.0;
    }
    return 0.0;
}

double TeslaSignalParser::extractThrottle(
    const std::vector<std::uint8_t>& data
) const {
    // Simple extraction - last byte as throttle (clamped)
    if (data.size() >= 1) {
        const double throttle = static_cast<double>(data.back());
        return std::max(0.0, std::min(100.0, throttle));
    }
    return 0.0;
}

double TeslaSignalParser::extractBrake(
    const std::vector<std::uint8_t>& data
) const {
    // Simple extraction for test
    if (data.size() >= 2) {
        const double brake = static_cast<double>(data[data.size() - 2]);
        return std::max(0.0, std::min(100.0, brake));
    }
    return 0.0;
}

double TeslaSignalParser::extractAcceleration(
    const std::vector<std::uint8_t>& data
) const {
    // Simple extraction for test - derive from speed change
    // For now, return 0 as we don't have history
    return 0.0;
}

std::uint64_t TeslaSignalParser::getCurrentTimestamp() const {
    const auto now = std::chrono::system_clock::now();
    const auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

} // namespace vehicle_sim::domain
