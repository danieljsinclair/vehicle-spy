#include "CanBridge.h"
#include <cstdio>
#include <algorithm>

namespace esp32_firmware {

CanBridge::CanBridge(ICanDriver& canDriver, ITcpClient& tcpClient, ISerialCan& serial)
    : canDriver_(canDriver), tcpClient_(tcpClient), serial_(serial) {}

bool CanBridge::init() {
    // TWAI init — listen-only so we never transmit on the vehicle bus
    // In real implementation, this would configure gcfg, tcfg, fcfg
    // For now, just mark as initialized
    initialized_ = true;
    return true;
}

void CanBridge::processFrames(bool monitorActive, uint32_t serialQuietUntilMs) {
    if (!initialized_) return;

    monitorActive_ = monitorActive;

    // Always drain the TWAI RX queue — each frame is received once and
    // dispatched to Serial unconditionally, and to TCP only when a client
    // is connected AND monitorActive.
    CanFrame msg;
    while (canDriver_.receive(&msg, 0) == 0) {  // ESP_OK = 0
        const bool suppressSerialFrames = (serialQuietUntilMs > 0);

        if (!suppressSerialFrames) {
            // Build frame string
            std::array<char, CanConfig::CAN_FRAME_BUFFER_SIZE> buf{};
            buildFrameString(msg, buf.data(), buf.size());

            // USB serial: always emit, independent of any TCP client
            serial_.print(buf.data());
        }

        // WiFi TCP: only when a client is connected and monitorActive
        if (tcpClient_.connected() && monitorActive_) {
            std::array<char, CanConfig::CAN_FRAME_BUFFER_SIZE> buf{};
            buildFrameString(msg, buf.data(), buf.size());
            tcpClient_.print(buf.data());
        }
    }
}

void CanBridge::buildFrameString(const CanFrame& msg, char* buf, size_t bufSize) {
    // Always build the frame in the canonical ELM327-ish text format
    // (3-digit zero-padded hex ID, space-separated byte hex)
    int n = std::snprintf(buf, bufSize, "%03X", msg.identifier);
    uint8_t len = std::min(msg.data_length_code, CanConfig::MAX_DATA_LENGTH);
    for (uint8_t i = 0; i < len; i++) {
        n += std::snprintf(buf + n, bufSize - n, " %02X", msg.data[i]);
    }
    std::snprintf(buf + n, bufSize - n, "\r");
}

} // namespace esp32_firmware